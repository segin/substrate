/*
 * ehci.c — EHCI (USB 2.0) host controller driver.
 *
 * Implements the usb_hcd_t interface (submit / port_status / port_reset /
 * port_enable) for a PCI EHCI controller (class 0x0C0320).  Synchronous,
 * polling model: one transfer at a time under submit_lock, matching uhci.c.
 *
 * A single asynchronous-schedule Queue Head (the "async head") is linked to
 * itself and reused for every control/bulk transfer: each transfer reprograms
 * its endpoint characteristics and points its overlay at a freshly-built qTD
 * chain, then polls the qTD tokens to completion.  qTDs/QH and a bounce buffer
 * live in DMA-coherent memory allocated once at init.
 *
 * Low/full-speed devices on a root port are released to a companion controller
 * (PORTSC OWNER); high-speed devices (usb-storage) are served here directly.
 */
#include <stdio.h>
#include <string.h>

#include <drivers/usb/ehci.h>
#include <drivers/usb/usb.h>
#include <kern/bus.h>
#include <kern/cmdline.h>
#include <kern/console.h>
#include <kern/driver.h>
#include <kern/pci.h>
#include <kern/resource.h>
#include <kern/time.h>
#include <sys/dma.h>
#include <sys/lock.h>
#include <vm/vm_kmem.h>

#define EHCI_BOUNCE_SIZE   (20 * 1024)   /* 5 qTD buffer pages */
#define EHCI_MAX_QTD       8
#define EHCI_XFER_TIMEOUT_MS 1000

typedef struct ehci_hc {
    volatile uint8_t *mmio;     /* BAR0 mapped base */
    volatile uint8_t *op;       /* operational regs = mmio + CAPLENGTH */
    uint8_t           nports;
    uint8_t           irq;
    int               initialized;

    mutex_t           submit_lock;
    /* Set when the controller is beyond use: the async schedule refused to
     * stop, or USBSTS reported the HC halted itself.  Every later submit
     * fails fast instead of burning its full timeout on a dead schedule. */
    int               hc_failed;

    struct ehci_qh   *async_qh;      dma_addr_t async_qh_dma;
    /* Periodic schedule: the frame list the controller walks once per frame,
     * and the single interrupt QH this driver links into it for the duration
     * of one polled transfer. [USB-10] */
    uint32_t         *periodic;      dma_addr_t periodic_dma;
    struct ehci_qh   *intr_qh;       dma_addr_t intr_qh_dma;
    struct ehci_qtd  *qtd;           dma_addr_t qtd_dma;   /* EHCI_MAX_QTD pool */
    void             *setup_buf;     dma_addr_t setup_dma;
    void             *bounce;        dma_addr_t bounce_dma;

    char              name[8];  /* "ehciN", backs hcd.name */
    struct device    *kdev;     /* bus device, for shutdown dispatch */
    usb_hcd_t         hcd;
} ehci_hc_t;

/* struct device carries no driver-private pointer, so shutdown dispatch
 * finds the controller by its device. [ehci-audit 7] */
#define EHCI_MAX_HCS 4
static ehci_hc_t *ehci_hcs[EHCI_MAX_HCS];

/*
 * One instance per PCI function.  A PC chipset commonly exposes two EHCI
 * controllers (one per companion group), and every controller past the first
 * used to be refused here -- leaving its root ports dead, and, because
 * ehci_port_reset() hands low/full-speed devices off to a companion
 * controller, leaving low-speed devices on *this* controller unreachable too
 * if that companion was also refused.
 */
static uint8_t ehci_instances;

/* ---- register access ---- */
static inline uint32_t ehci_op_rd(ehci_hc_t *hc, uint32_t reg)
{
    return *(volatile uint32_t *)(hc->op + reg);
}
static inline void ehci_op_wr(ehci_hc_t *hc, uint32_t reg, uint32_t val)
{
    *(volatile uint32_t *)(hc->op + reg) = val;
}
static inline uint32_t ehci_portsc_rd(ehci_hc_t *hc, uint8_t port)
{
    return ehci_op_rd(hc, EHCI_OP_PORTSC + (uint32_t)(port - 1) * 4);
}
static inline void ehci_portsc_wr(ehci_hc_t *hc, uint8_t port, uint32_t val)
{
    ehci_op_wr(hc, EHCI_OP_PORTSC + (uint32_t)(port - 1) * 4, val);
}

static void ehci_delay_ms(uint32_t ms)
{
    uint64_t deadline = (uint64_t)get_uptime_ms() + ms;
    while ((uint64_t)get_uptime_ms() < deadline)
        __asm__ volatile("pause");
}

/* ---- port operations ---- */
static uint32_t ehci_port_status(usb_hcd_t *hcd, uint8_t port)
{
    ehci_hc_t *hc = hcd->priv;
    uint32_t psc = ehci_portsc_rd(hc, port);
    uint32_t out = 0;
    if (psc & EHCI_PORT_CONNECT) out |= USB_PORT_STAT_CONNECTION;
    if (psc & EHCI_PORT_ENABLE)  out |= USB_PORT_STAT_ENABLE;
    /* A device that stays enabled after an EHCI reset is high-speed. */
    if (psc & EHCI_PORT_ENABLE)  out |= USB_PORT_STAT_HIGH_SPEED;
    return out;
}

static int ehci_port_reset(usb_hcd_t *hcd, uint8_t port)
{
    ehci_hc_t *hc = hcd->priv;
    uint32_t psc = ehci_portsc_rd(hc, port);

    if (!(psc & EHCI_PORT_CONNECT))
        return -1;

    /* A low-speed device parks the line in K state -- hand it to a companion
     * controller instead of trying to reset it here (EHCI is high-speed only). */
    if ((psc & EHCI_PORT_LINESTATUS) == EHCI_PORT_LS_KSTATE) {
        ehci_portsc_wr(hc, port, psc | EHCI_PORT_OWNER);
        return -1;
    }

    /* Assert reset for 50ms (clear Enable + preserve W1C-safe bits). */
    psc = ehci_portsc_rd(hc, port);
    psc &= ~EHCI_PORT_ENABLE;
    psc &= ~(EHCI_PORT_CONNECT_CH | EHCI_PORT_ENABLE_CH | EHCI_PORT_OC_CH);
    psc |= EHCI_PORT_RESET;
    ehci_portsc_wr(hc, port, psc);
    ehci_delay_ms(50);

    /* Deassert; the controller completes the reset and sets Enable if the
     * device is high-speed (else it releases the port to a companion). */
    psc = ehci_portsc_rd(hc, port);
    psc &= ~EHCI_PORT_RESET;
    ehci_portsc_wr(hc, port, psc);

    /* Wait up to 50ms for the reset to finish + the port to enable. */
    for (int i = 0; i < 50; i++) {
        ehci_delay_ms(1);
        psc = ehci_portsc_rd(hc, port);
        if (!(psc & EHCI_PORT_RESET))
            break;
    }
    psc = ehci_portsc_rd(hc, port);
    if (psc & EHCI_PORT_RESET) {
        /* [PORT-02] Reset never completed -- the spec bound is 2 ms after
         * the PR deassert, so a PR still set here means a faulted/halted
         * controller (Table 2-16: the HC may hold Port Reset asserted when
         * HCHalted is one).  PED is meaningless before PR reads 0 (4.2.2);
         * falling through misread it as "not high-speed" and handed a port
         * with PR STILL ASSERTED to a companion. */
        kprintf("ehci: port %u reset timeout\n", port);
        return -1;
    }
    if (!(psc & EHCI_PORT_ENABLE)) {
        /* Not high-speed: release to a companion controller. */
        ehci_portsc_wr(hc, port, psc | EHCI_PORT_OWNER);
        return -1;
    }
    return 0;
}

static int ehci_port_enable(usb_hcd_t *hcd, uint8_t port, int enable)
{
    ehci_hc_t *hc = hcd->priv;
    uint32_t psc = ehci_portsc_rd(hc, port);
    psc &= ~(EHCI_PORT_CONNECT_CH | EHCI_PORT_ENABLE_CH | EHCI_PORT_OC_CH);
    if (enable) psc |= EHCI_PORT_ENABLE; else psc &= ~EHCI_PORT_ENABLE;
    ehci_portsc_wr(hc, port, psc);
    return 0;
}

/* ---- qTD helpers ---- */

/* Program one qTD: PID, byte count, toggle, buffer pointers into `bounce`.
 *
 * alt_dma is the Alternate Next qTD pointer, followed after a SHORT packet.
 * The spec contradicts itself about an alt pointer with the T-bit set:
 * 4.10.2 falls back to the Next pointer, but 3.5.2 says the controller
 * "will always use this pointer when the current qTD is retired due to
 * short packet" -- i.e. 3.5.2-literal silicon treats alt=T as end-of-queue
 * and never fetches the qTDs behind it.  QEMU and ICH implement the 4.10.2
 * reading, which is why bare alt=T worked here; both BSDs still refuse to
 * rely on it (NetBSD points altnext at the real next stage, FreeBSD at a
 * pre-halted dummy).  Pass 0 for end-of-chain semantics. [ehci-audit 6] */
static void ehci_fill_qtd(ehci_hc_t *hc, int idx, uint32_t next_dma,
                          uint32_t alt_dma, uint32_t pid, uint32_t bytes,
                          int toggle, dma_addr_t data_dma, int ioc)
{
    struct ehci_qtd *q = &hc->qtd[idx];
    memset(q, 0, sizeof(*q));
    q->next = next_dma ? next_dma : EHCI_LINK_TERMINATE;
    q->alt_next = alt_dma ? alt_dma : EHCI_LINK_TERMINATE;
    q->token = EHCI_QTD_STATUS_ACTIVE | pid |
               (3u << EHCI_QTD_CERR_SHIFT) |
               ((bytes & 0x7FFF) << EHCI_QTD_BYTES_SHIFT) |
               (toggle ? EHCI_QTD_TOGGLE : 0) |
               (ioc ? EHCI_QTD_IOC : 0);
    if (bytes) {
        uint32_t base = (uint32_t)data_dma;
        for (int p = 0; p < 5; p++) {
            if (p == 0)
                q->buffer[0] = base;
            else
                q->buffer[p] = (base & ~0xFFFu) + (uint32_t)p * 0x1000u;
        }
    }
}

static uint32_t ehci_qtd_dma(ehci_hc_t *hc, int idx)
{
    return (uint32_t)(hc->qtd_dma + (dma_addr_t)idx * sizeof(struct ehci_qtd));
}

/* [DRV-05] On a transfer timeout the controller may still own the async QH
 * overlay and keep DMAing into the (reused) bounce buffer / walking the qTDs.
 * It has to be verifiably stopped before that memory is touched.  4.8 also
 * forbids modifying ASE unless it equals ASS, so the stop must be confirmed
 * before anything -- including ASE itself -- is written again.  [ASYNC-04]
 * Returns 0 only when the schedule is verifiably stopped (ASS clear). */
static int ehci_async_stop(ehci_hc_t *hc)
{
    uint32_t cmd = ehci_op_rd(hc, EHCI_OP_USBCMD);
    ehci_op_wr(hc, EHCI_OP_USBCMD, cmd & ~EHCI_CMD_ASE);
    for (int i = 0; i < 100; i++) {
        if (!(ehci_op_rd(hc, EHCI_OP_USBSTS) & EHCI_STS_ASS))
            return 0;
        ehci_delay_ms(1);
    }
    return -1;
}

static void ehci_async_restart(ehci_hc_t *hc)
{
    uint32_t cmd = ehci_op_rd(hc, EHCI_OP_USBCMD);
    ehci_op_wr(hc, EHCI_OP_USBCMD, cmd | EHCI_CMD_ASE);
    for (int i = 0; i < 100; i++) {
        if (ehci_op_rd(hc, EHCI_OP_USBSTS) & EHCI_STS_ASS)
            break;
        ehci_delay_ms(1);
    }
}

/* [EHCI-05] Classify a halted qTD.  A pure STALL handshake halts the queue
 * with only the Halted bit set; babble, transaction-error, buffer-error and
 * missed-microframe halts carry their cause bit alongside it (Table 3-16).
 * Reporting them all as USB_XFER_STALL made callers run stall recovery
 * (usb_clear_halt) against endpoints that had a transport error instead --
 * and let one transient error-counter exhaustion on a GET_REPORT permanently
 * latch a HID device's ctl_poll_refused. */
static int ehci_halt_status(uint32_t tok)
{
    if (tok & (EHCI_QTD_STATUS_XACTERR | EHCI_QTD_STATUS_BABBLE |
               EHCI_QTD_STATUS_BUFERR | EHCI_QTD_STATUS_MISSED))
        return USB_XFER_ERROR;
    return USB_XFER_STALL;
}

/* Arm the async QH at a qTD chain and poll to completion.  Returns USB_XFER_*.
 * [DRV-06] n_qtd counts the qTDs this transfer filled/linked (contiguous from
 * first_qtd); only those are polled, so stale qTDs left ACTIVE/HALTED by a prior
 * transfer (e.g. a STALLed GET_MAX_LUN) no longer poison this one. */
static int ehci_run_qh(ehci_hc_t *hc, int first_qtd, int n_qtd, uint32_t endp_char,
                       uint32_t timeout_ms, uint32_t endp_cap)
{
    /* [EHCI-01] The async QH is permanently reachable (self-linked, ASE on)
     * and an inactive non-halted overlay is advanceable the instant
     * overlay_next becomes valid (4.10.2), so the old order (next first,
     * token last) let the controller seize the qTD chain mid-update -- and
     * the trailing overlay_token=0 store then scribbled over an overlay the
     * controller owned.  A Halted overlay is skipped at every Fetch-QH
     * (Fig 4-14 / 4.10.5): park it with one atomic store, program the rest,
     * and publish with one final token store (NetBSD ehci_set_qh_qtd does
     * the same).  volatile + barriers keep the compiler from reordering or
     * eliding the park. */
    volatile struct ehci_qh *qh = hc->async_qh;
    qh->overlay_token = EHCI_QTD_STATUS_HALTED;
    __asm__ volatile("" ::: "memory");

    qh->endp_char = endp_char | EHCI_QH_HEAD | EHCI_QH_DTC;
    qh->endp_cap  = endp_cap;
    qh->current_qtd = 0;
    qh->overlay_next = ehci_qtd_dma(hc, first_qtd);
    qh->overlay_alt_next = EHCI_LINK_TERMINATE;
    for (int p = 0; p < 5; p++) {
        qh->overlay_buffer[p] = 0;
        qh->overlay_buffer_hi[p] = 0;
    }

    /* Barrier: the qTD fills and the stores above must be in memory before
     * the controller is allowed back into the overlay. */
    __asm__ volatile("" ::: "memory");
    qh->overlay_token = 0;   /* not active/halted -> controller reloads from next */

    /* Poll the qTD chain until none is Active, or a Halt, or timeout. */
    uint64_t deadline = (uint64_t)get_uptime_ms() + timeout_ms;
    for (;;) {
        int active = 0, halted = 0;
        uint32_t htok = 0;
        for (int i = first_qtd; i < first_qtd + n_qtd; i++) {   /* [DRV-06] this xfer only */
            uint32_t tok = hc->qtd[i].token;
            /* only inspect qTDs we linked (token nonzero means we set it) */
            if (!(tok & (EHCI_QTD_STATUS_ACTIVE | EHCI_QTD_STATUS_ERRMASK)))
                continue;
            if (tok & EHCI_QTD_STATUS_ACTIVE) active = 1;
            if (tok & EHCI_QTD_STATUS_HALTED) { halted = 1; htok = tok; }
        }
        if (halted) return ehci_halt_status(htok);
        if (!active) break;
        if ((uint64_t)get_uptime_ms() > deadline) {
            /* [DRV-05] Reclaim the descriptors from the hardware before the
             * caller reuses the bounce buffer / qTD pool. */
            if (ehci_async_stop(hc) != 0) {
                /* [ASYNC-04] Schedule refused to stop: do not scribble on
                 * memory the HC may still walk, and do not flip ASE against
                 * the 4.8 ASE==ASS rule.  This controller is done. */
                hc->hc_failed = 1;
                kprintf("ehci: async schedule failed to stop; "
                        "controller disabled\n");
                return USB_XFER_TIMEOUT;
            }
            /* [EHCI-03] A completion may have landed between the last token
             * sample and the stop: re-read before destroying the evidence,
             * or a just-finished transfer is thrown away as a timeout. */
            int late_active = 0, late_halted = 0;
            uint32_t late_htok = 0;
            for (int i = first_qtd; i < first_qtd + n_qtd; i++) {
                uint32_t tok = hc->qtd[i].token;
                if (!(tok & (EHCI_QTD_STATUS_ACTIVE | EHCI_QTD_STATUS_ERRMASK)))
                    continue;
                if (tok & EHCI_QTD_STATUS_ACTIVE) late_active = 1;
                if (tok & EHCI_QTD_STATUS_HALTED) {
                    late_halted = 1;
                    late_htok = tok;
                }
            }
            if (!late_active || late_halted) {
                ehci_async_restart(hc);
                return late_halted ? ehci_halt_status(late_htok)
                                   : USB_XFER_OK;
            }
            /* Genuinely incomplete: neutralize so a stale visit is a no-op.
             * Preserve the overlay dt -- it is the authoritative ending
             * toggle for the resync in the bulk path ([EHCI-04]). */
            for (int i = first_qtd; i < first_qtd + n_qtd; i++)
                hc->qtd[i].token &= ~EHCI_QTD_STATUS_ACTIVE;
            struct ehci_qh *aqh = hc->async_qh;
            aqh->overlay_next = EHCI_LINK_TERMINATE;
            aqh->overlay_alt_next = EHCI_LINK_TERMINATE;
            aqh->overlay_token = (aqh->overlay_token & EHCI_QTD_TOGGLE) |
                                 EHCI_QTD_STATUS_HALTED;
            ehci_async_restart(hc);
            return USB_XFER_TIMEOUT;
        }
        __asm__ volatile("pause");
    }
    return USB_XFER_OK;
}

/*
 * Build the EHCI QH endpoint-characteristics word for this transfer.
 *
 * The speed comes from the device, not a constant: every queue head used to be
 * built as EHCI_QH_EPS_HIGH, so a full- or low-speed device was described to
 * the controller as something it is not and could never transfer.  A non-
 * high-speed CONTROL endpoint additionally needs the Control Endpoint Flag so
 * the controller knows to use the control split protocol. [USB-02]
 */
static uint32_t ehci_endp_char(usb_transfer_t *xfer, int is_control)
{
    usb_device_t *dev = xfer->dev;
    uint32_t addr = dev->address & 0x7F;
    uint32_t ep   = xfer->ep ? (xfer->ep->address & 0x0F) : 0;
    uint32_t mpl  = xfer->ep ? xfer->ep->max_packet : 64;
    uint32_t eps;
    uint32_t ec;

    if (mpl == 0) mpl = is_control ? 64 : 512;

    switch (dev->speed) {
    case USB_SPEED_LOW:  eps = EHCI_QH_EPS_LOW;  break;
    case USB_SPEED_FULL: eps = EHCI_QH_EPS_FULL; break;
    default:             eps = EHCI_QH_EPS_HIGH; break;
    }

    ec = (addr << EHCI_QH_ADDR_SHIFT) |
         (ep   << EHCI_QH_ENDPT_SHIFT) |
         eps |
         ((mpl & USB_EP_MPS_MASK) << EHCI_QH_MPL_SHIFT);

    if (is_control && dev->speed != USB_SPEED_HIGH)
        ec |= EHCI_QH_CONTROL_EP;

    /* NAK count reload: 4 for a high-speed ASYNCHRONOUS endpoint, 0
     * otherwise.  A split transaction must not be abandoned on NAKs -- and
     * neither may an interrupt endpoint: 4.9 requires RL=0 ("Not Used") for
     * interrupt queue heads, because the reload machinery (4.9.1) runs only
     * during the async reclamation-list traversal.  A periodic QH with
     * RL!=0 on throttling silicon decrements NakCnt to zero after 4 NAKs
     * and is then never considered for execution again -- the endpoint goes
     * deaf for the rest of the transfer window.  QEMU does not implement
     * the throttle, which is why this never showed there. [ehci-audit 5] */
    int is_intr = xfer->ep && xfer->ep->type == USB_EP_TYPE_INTERRUPT;
    if (dev->speed == USB_SPEED_HIGH && !is_intr)
        ec |= 4u << EHCI_QH_NRL_SHIFT;

    return ec;
}

/*
 * Build the endpoint-capabilities word: the pipe multiplier plus, for a
 * full/low-speed device, the address and port of the high-speed hub whose
 * transaction translator bridges it.  Interrupt endpoints also need the
 * start-split / complete-split microframe masks; the async schedule does not
 * use them, which is why bulk and control leave them clear. [USB-02]
 */
static uint32_t ehci_endp_cap(usb_transfer_t *xfer, uint8_t mult)
{
    usb_device_t *dev = xfer->dev;
    uint32_t cap = (uint32_t)mult << EHCI_QH_MULT_SHIFT;
    int is_intr = xfer->ep && xfer->ep->type == USB_EP_TYPE_INTERRUPT;

    if (is_intr)
        cap |= (1u << 1) << EHCI_QH_SMASK_SHIFT;   /* start split in Y1 */

    if (dev->speed != USB_SPEED_HIGH) {
        uint8_t ttport = 0;
        usb_device_t *tthub = usb_tt_hub(dev, &ttport);

        if (tthub) {
            cap |= ((uint32_t)(tthub->address & 0x7F) << EHCI_QH_HUBA_SHIFT) |
                   ((uint32_t)(ttport & 0x7F) << EHCI_QH_PORT_SHIFT);
        }
        if (is_intr)
            cap |= (0x7u << 3) << EHCI_QH_CMASK_SHIFT;  /* complete splits Y3-Y5 */
    }
    return cap;
}

static int ehci_control_transfer(ehci_hc_t *hc, usb_transfer_t *xfer)
{
    uint32_t len = xfer->length;
    int in = (xfer->setup.bmRequestType & 0x80) != 0;
    if (len > EHCI_BOUNCE_SIZE)
        return USB_XFER_ERROR;

    /* SETUP stage */
    memcpy(hc->setup_buf, &xfer->setup, sizeof(struct usb_setup_packet));
    if (!in && len)
        memcpy(hc->bounce, xfer->data, len);

    int idx = 0;
    int setup_i = idx++;
    int data_i  = len ? idx++ : -1;
    int status_i = idx++;

    /* status stage direction is opposite the data stage (IN if no data). */
    uint32_t status_pid = (in && len) ? EHCI_QTD_PID_OUT : EHCI_QTD_PID_IN;

    ehci_fill_qtd(hc, status_i, 0, 0, status_pid, 0, 1, 0, 1);
    if (data_i >= 0) {
        uint32_t dpid = in ? EHCI_QTD_PID_IN : EHCI_QTD_PID_OUT;
        /* The data qTD's alt_next points at the STATUS qTD: a short IN data
         * stage then reaches the status stage under both spec readings
         * (4.10.2-HCs take Next, 3.5.2-HCs take Alt -- same destination).
         * With bare alt=T, 3.5.2-literal silicon abandoned EP0 mid-transfer
         * after any short read (concrete trigger: the 11-byte hub
         * descriptor request that a small hub answers with 9). */
        ehci_fill_qtd(hc, data_i, ehci_qtd_dma(hc, status_i),
                      ehci_qtd_dma(hc, status_i), dpid, len, 1,
                      hc->bounce_dma, 0);
        ehci_fill_qtd(hc, setup_i, ehci_qtd_dma(hc, data_i), 0,
                      EHCI_QTD_PID_SETUP, 8, 0, hc->setup_dma, 0);
    } else {
        ehci_fill_qtd(hc, setup_i, ehci_qtd_dma(hc, status_i), 0,
                      EHCI_QTD_PID_SETUP, 8, 0, hc->setup_dma, 0);
    }

    /* Honour the caller's timeout: a HID poll asks to give up in milliseconds,
     * and making it sit out the bulk timeout stalled the USB thread for a full
     * second per idle poll. [USB-09] */
    int r = ehci_run_qh(hc, setup_i, idx, ehci_endp_char(xfer, 1), /* [DRV-06] idx qTDs */
                        xfer->timeout_ms ? xfer->timeout_ms
                                         : EHCI_XFER_TIMEOUT_MS,
                        ehci_endp_cap(xfer, 1)); /* control is never high-bandwidth */
    if (r == USB_XFER_OK && in && len) {
        /* bytes actually moved = requested - residue from the data qTD */
        uint32_t residue = (hc->qtd[data_i].token >> EHCI_QTD_BYTES_SHIFT) & 0x7FFF;
        xfer->actual_length = len - residue;
        memcpy(xfer->data, hc->bounce, xfer->actual_length);
    } else if (r == USB_XFER_OK) {
        xfer->actual_length = len;
    }
    xfer->status = r;
    return r;
}

static int ehci_bulk_transfer(ehci_hc_t *hc, usb_transfer_t *xfer)
{
    uint32_t len = xfer->length;
    int in = xfer->ep && (xfer->ep->address & 0x80);
    if (len > EHCI_BOUNCE_SIZE)
        return USB_XFER_ERROR;

    if (!in && len)
        memcpy(hc->bounce, xfer->data, len);

    uint32_t pid = in ? EHCI_QTD_PID_IN : EHCI_QTD_PID_OUT;
    int toggle = xfer->ep ? xfer->ep->toggle : 0;
    ehci_fill_qtd(hc, 0, 0, 0, pid, len, toggle, len ? hc->bounce_dma : 0, 1);

    int r = ehci_run_qh(hc, 0, 1, ehci_endp_char(xfer, 0), /* [DRV-06] single qTD */
                        xfer->timeout_ms ? xfer->timeout_ms
                                         : EHCI_XFER_TIMEOUT_MS,   /* [USB-09] */
                        ehci_endp_cap(xfer,
                                      xfer->ep && xfer->ep->mult ? xfer->ep->mult
                                                                 : 1));
    if (r == USB_XFER_OK) {
        uint32_t residue = (hc->qtd[0].token >> EHCI_QTD_BYTES_SHIFT) & 0x7FFF;
        xfer->actual_length = len - residue;
        if (in && xfer->actual_length)
            memcpy(xfer->data, hc->bounce, xfer->actual_length);
    }
    if ((r == USB_XFER_OK || r == USB_XFER_TIMEOUT) && xfer->ep &&
        hc->async_qh->current_qtd) {
        /* [EHCI-04] The ending toggle used to be computed as
         * initial ^ (npackets & 1) from actual_length -- which misses a
         * device's terminating ZLP (a bulk IN holding exactly k*MPS bytes
         * but fewer than requested sends k full packets plus a ZLP: k+1
         * toggles, parity of k computed) and was never run on timeouts even
         * though packets may have moved before the deadline.  The QH
         * overlay dt is written back after every transaction (4.10.3) and
         * IS the next expected toggle; the qTD copy is not guaranteed to
         * carry dt (4.10.4 requires only Total Bytes/CErr/Status).  Gate on
         * current_qtd != 0: if the controller never advanced into our
         * chain, the overlay token still holds the driver's own arming
         * value and says nothing.  Leave STALL to usb_clear_halt's toggle
         * reset. */
        xfer->ep->toggle =
            (*(volatile uint32_t *)&hc->async_qh->overlay_token &
             EHCI_QTD_TOGGLE) ? 1 : 0;
    }
    xfer->status = r;
    return r;
}

/*
 * Run one interrupt transfer through the PERIODIC schedule.
 *
 * These used to be handed to ehci_bulk_transfer(), i.e. queued on the
 * asynchronous ring.  That transfers -- the device NAKs until it has data --
 * but the async ring is walked as fast as the controller can cycle it, so
 * bInterval was ignored entirely and an idle endpoint burned async bandwidth
 * continuously.  EHCI's periodic schedule is what implements a polling
 * interval: the controller walks one frame-list entry per 1 ms frame, so a QH
 * linked in every Nth entry is visited every N milliseconds. [USB-10]
 *
 * The driver is synchronous and holds submit_lock, so a single QH is linked in,
 * polled, and unlinked per transfer rather than kept resident.
 */
static int ehci_intr_transfer(ehci_hc_t *hc, usb_transfer_t *xfer)
{
    uint32_t len = xfer->length;
    int in = xfer->ep && (xfer->ep->address & 0x80);
    struct ehci_qh *qh = hc->intr_qh;
    uint32_t stride, qh_link;
    uint64_t deadline;
    int r = USB_XFER_OK;

    if (len > EHCI_BOUNCE_SIZE)
        return USB_XFER_ERROR;
    if (!in && len)
        memcpy(hc->bounce, xfer->data, len);

    /*
     * bInterval -> frame stride.  High speed encodes it as 2^(bInterval-1)
     * MICROframes (8 per frame); full and low speed give frames directly.
     * Clamp into the frame list so the QH is always reachable.
     */
    {
        uint8_t bi = xfer->ep ? xfer->ep->interval : 1;
        if (bi == 0)
            bi = 1;
        if (xfer->dev->speed == USB_SPEED_HIGH) {
            uint32_t uframes = 1u << (bi > 16 ? 15 : (bi - 1));
            stride = uframes / 8;
        } else {
            stride = bi;
        }
        if (stride == 0)
            stride = 1;
        if (stride > EHCI_FRAMELIST_ENTRIES / 2)
            stride = EHCI_FRAMELIST_ENTRIES / 2;
    }

    /* One qTD, at the front of the pool -- submit_lock serialises us against
     * the async path that also uses it. */
    ehci_fill_qtd(hc, 0, 0, 0, in ? EHCI_QTD_PID_IN : EHCI_QTD_PID_OUT, len,
                  xfer->ep ? xfer->ep->toggle : 0, len ? hc->bounce_dma : 0, 1);

    /* A periodic QH is NOT the head of a reclamation list: EHCI_QH_HEAD is an
     * async-ring-only flag and setting it here would make the controller treat
     * this QH as the start of the async list. */
    qh->endp_char = ehci_endp_char(xfer, 0) | EHCI_QH_DTC;
    qh->endp_cap  = ehci_endp_cap(xfer, xfer->ep && xfer->ep->mult
                                        ? xfer->ep->mult : 1);
    qh->hlink     = EHCI_LINK_TERMINATE;
    qh->current_qtd = 0;
    qh->overlay_next = ehci_qtd_dma(hc, 0);
    qh->overlay_alt_next = EHCI_LINK_TERMINATE;
    qh->overlay_token = 0;

    /* Link it into every stride-th frame. */
    qh_link = (uint32_t)hc->intr_qh_dma | EHCI_LINK_TYPE_QH;
    for (uint32_t i = 0; i < EHCI_FRAMELIST_ENTRIES; i += stride)
        hc->periodic[i] = qh_link;

    deadline = (uint64_t)get_uptime_ms() +
               (xfer->timeout_ms ? xfer->timeout_ms : EHCI_XFER_TIMEOUT_MS);
    for (;;) {
        uint32_t tok = hc->qtd[0].token;
        if (tok & EHCI_QTD_STATUS_HALTED) { r = ehci_halt_status(tok); break; }
        if (!(tok & EHCI_QTD_STATUS_ACTIVE)) break;
        if ((uint64_t)get_uptime_ms() > deadline) { r = USB_XFER_TIMEOUT; break; }
        __asm__ volatile("pause");
    }

    /*
     * Unlink before touching the buffer again.  There is no doorbell handshake
     * for the periodic schedule (IAAD covers the async ring only), so terminate
     * the frame-list entries and then let the controller advance past any frame
     * it might already be executing: two frames of FRINDEX movement is enough,
     * and the loop is bounded so a stopped controller cannot hang us.
     */
    for (uint32_t i = 0; i < EHCI_FRAMELIST_ENTRIES; i += stride)
        hc->periodic[i] = EHCI_LINK_TERMINATE;
    {
        uint32_t start = ehci_op_rd(hc, EHCI_OP_FRINDEX) >> 3;
        for (int guard = 0; guard < 20; guard++) {
            uint32_t now = ehci_op_rd(hc, EHCI_OP_FRINDEX) >> 3;
            if (((now - start) & (EHCI_FRAMELIST_ENTRIES - 1)) >= 2)
                break;
            ehci_delay_ms(1);
        }
    }
    /* [EHCI-03] A completion can land between the last poll and the unlink
     * settling above; re-read the token before neutralizing it, or a
     * just-delivered report is discarded as a timeout. */
    if (r == USB_XFER_TIMEOUT) {
        uint32_t tok = hc->qtd[0].token;
        if (!(tok & EHCI_QTD_STATUS_ACTIVE))
            r = (tok & EHCI_QTD_STATUS_HALTED) ? ehci_halt_status(tok)
                                               : USB_XFER_OK;
    }
    /* [EHCI-04] Capture the overlay before neutralizing it below -- the
     * write-back overlay dt is the authoritative ending toggle. */
    uint32_t ov_tok = *(volatile uint32_t *)&qh->overlay_token;
    uint32_t ov_cur = qh->current_qtd;
    hc->qtd[0].token &= ~EHCI_QTD_STATUS_ACTIVE;
    qh->overlay_next = EHCI_LINK_TERMINATE;
    qh->overlay_alt_next = EHCI_LINK_TERMINATE;
    qh->overlay_token = EHCI_QTD_STATUS_HALTED;

    if (r == USB_XFER_OK) {
        uint32_t residue = (hc->qtd[0].token >> EHCI_QTD_BYTES_SHIFT) & 0x7FFF;
        xfer->actual_length = (len > residue) ? (len - residue) : 0;
        if (in && xfer->actual_length)
            memcpy(xfer->data, hc->bounce, xfer->actual_length);
    }
    if ((r == USB_XFER_OK || r == USB_XFER_TIMEOUT) && xfer->ep && ov_cur) {
        /* [EHCI-04] Same overlay-dt readback as the bulk path: parity from
         * actual_length misses a terminating ZLP and skips timeouts. */
        xfer->ep->toggle = (ov_tok & EHCI_QTD_TOGGLE) ? 1 : 0;
    }
    xfer->status = r;
    return r;
}

static int ehci_submit(usb_hcd_t *hcd, usb_transfer_t *xfer)
{
    ehci_hc_t *hc = hcd->priv;
    int ret;
    mutex_lock(&hc->submit_lock);
    if (hc->hc_failed) {
        /* Dead controller: fail fast rather than burning the full transfer
         * timeout against a schedule that will never run it. [ASYNC-04] */
        xfer->status = USB_XFER_ERROR;
        mutex_unlock(&hc->submit_lock);
        return USB_XFER_ERROR;
    }
    if (xfer->is_control)
        ret = ehci_control_transfer(hc, xfer);
    else if (xfer->ep && xfer->ep->type == USB_EP_TYPE_BULK)
        ret = ehci_bulk_transfer(hc, xfer);
    else if (xfer->ep && xfer->ep->type == USB_EP_TYPE_INTERRUPT)
        ret = ehci_intr_transfer(hc, xfer);   /* periodic schedule [USB-10] */
    else
        ret = USB_XFER_ERROR;
    mutex_unlock(&hc->submit_lock);
    return ret;
}

/* ---- controller init ---- */

/*
 * Take the controller away from the firmware.
 *
 * A BIOS that emulates a PS/2 keyboard from a USB one keeps owning the EHCI
 * through SMM until the OS asks for it via the USB Legacy Support extended
 * capability.  Resetting a controller SMM is still driving produces exactly the
 * intermittent enumeration failures real hardware shows and QEMU never does —
 * QEMU leaves the BIOS semaphore clear, so this is a no-op there.
 *
 * EHCI's extended capabilities live in PCI config space (unlike xHCI's, which
 * are in the MMIO capability region), reached through HCCPARAMS' EECP field.
 */
static void ehci_take_controller(ehci_hc_t *hc, pci_device_t *pdev)
{
    uint32_t hcc = *(volatile uint32_t *)(hc->mmio + EHCI_CAP_HCCPARAMS);
    uint8_t off = EHCI_HCCPARAMS_EECP(hcc);

    /* The list lives above the standard 64-byte config header; bound the walk
     * so a garbage next-pointer cannot spin here forever. */
    for (int guard = 0; off >= 0x40 && guard < 32; guard++) {
        uint32_t cap = pci_read_config32(pdev->bus, pdev->slot, pdev->func, off);

        if (cap == 0xFFFFFFFFu)
            break;
        if (EHCI_EECP_ID(cap) == EHCI_ECAP_ID_LEGACY) {
            uint8_t bios_sem;
            int i;

            bios_sem = pci_read_config8(pdev->bus, pdev->slot, pdev->func,
                                        off + EHCI_LEGSUP_BIOS_SEM);
            if (bios_sem) {
                kprintf("ehci: waiting up to 5s for the BIOS to release the "
                        "controller\n");
                pci_write_config8(pdev->bus, pdev->slot, pdev->func,
                                  off + EHCI_LEGSUP_OS_SEM, 1);
                for (i = 0; i < 5000; i++) {
                    /* Tick once a second.  Without this the wait looks
                     * indistinguishable from a hang, which is precisely how it
                     * was first reported. */
                    if (i && (i % 1000) == 0)
                        kprintf("ehci: still waiting (%ds)\n", i / 1000);
                    bios_sem = pci_read_config8(pdev->bus, pdev->slot, pdev->func,
                                                off + EHCI_LEGSUP_BIOS_SEM);
                    if (bios_sem == 0)
                        break;
                    ehci_delay_ms(1);
                }
                if (bios_sem) {
                    /* Buggy firmware that never drops the semaphore.  Clear it
                     * ourselves — we are about to reset the controller either
                     * way, so leaving SMM believing it still owns the hardware
                     * is the worse outcome. */
                    kprintf("ehci: BIOS never released the controller after "
                            "5s; claiming it anyway\n");
                    pci_write_config8(pdev->bus, pdev->slot, pdev->func,
                                      off + EHCI_LEGSUP_BIOS_SEM, 0);
                }
                else
                    kprintf("ehci: BIOS released the controller after %dms\n", i);
            }
            /* Disarm every SMI source and acknowledge what is pending. */
            pci_write_config32(pdev->bus, pdev->slot, pdev->func,
                               off + EHCI_LEGSUP_CTLSTS, 0);
        }
        off = EHCI_EECP_NEXT(cap);
    }
}

static int ehci_reset_controller(ehci_hc_t *hc)
{
    /* Halt, then HCReset. */
    uint32_t cmd = ehci_op_rd(hc, EHCI_OP_USBCMD);
    ehci_op_wr(hc, EHCI_OP_USBCMD, cmd & ~EHCI_CMD_RUN);
    for (int i = 0; i < 100; i++) {
        if (ehci_op_rd(hc, EHCI_OP_USBSTS) & EHCI_STS_HCHALTED) break;
        ehci_delay_ms(1);
    }
    ehci_op_wr(hc, EHCI_OP_USBCMD, EHCI_CMD_HCRESET);
    for (int i = 0; i < 250; i++) {
        if (!(ehci_op_rd(hc, EHCI_OP_USBCMD) & EHCI_CMD_HCRESET)) return 0;
        ehci_delay_ms(1);
    }
    return -1;
}

/*
 * Bring-up trace, and an off switch.
 *
 * On a PCH where xhci_intel_port_switch() has moved every USB2 port to the
 * xHCI, this controller has no ports left to drive -- attaching it is all risk
 * and no benefit, and it is the last thing running before the boot stops on a
 * Lenovo C460.  "noehci" skips it entirely; "ehcidebug" narrates the bring-up
 * so a hang names the register access that caused it.
 */
static int ehci_trace;
#define EHCI_STEP(msg) do { if (ehci_trace) kprintf("ehci: " msg "\n"); } while (0)

static int ehci_start(ehci_hc_t *hc)
{
    EHCI_STEP("resetting controller");
    if (ehci_reset_controller(hc) != 0) {
        kprintf("ehci: controller reset timeout\n");
        return -1;
    }

    EHCI_STEP("reset complete");

    /* Allocate DMA structures (once). */
    hc->async_qh = dma_alloc_coherent(sizeof(struct ehci_qh), &hc->async_qh_dma);
    hc->qtd      = dma_alloc_coherent(EHCI_MAX_QTD * sizeof(struct ehci_qtd), &hc->qtd_dma);
    hc->setup_buf = dma_alloc_coherent(64, &hc->setup_dma);
    hc->bounce    = dma_alloc_coherent(EHCI_BOUNCE_SIZE, &hc->bounce_dma);
    hc->periodic  = dma_alloc_coherent(EHCI_FRAMELIST_ENTRIES * sizeof(uint32_t),
                                       &hc->periodic_dma);
    hc->intr_qh   = dma_alloc_coherent(sizeof(struct ehci_qh), &hc->intr_qh_dma);
    if (!hc->async_qh || !hc->qtd || !hc->setup_buf || !hc->bounce ||
        !hc->periodic || !hc->intr_qh) {
        kprintf("ehci: DMA allocation failed\n");
        return -1;
    }

    /* Async ring: one head QH linked to itself, overlay terminated. */
    memset(hc->async_qh, 0, sizeof(struct ehci_qh));
    hc->async_qh->hlink = (uint32_t)hc->async_qh_dma | EHCI_LINK_TYPE_QH;
    hc->async_qh->endp_char = EHCI_QH_HEAD | EHCI_QH_DTC;
    hc->async_qh->overlay_next = EHCI_LINK_TERMINATE;
    hc->async_qh->overlay_alt_next = EHCI_LINK_TERMINATE;
    hc->async_qh->overlay_token = EHCI_QTD_STATUS_HALTED;

    /* Periodic frame list: every entry terminated until an interrupt transfer
     * links its QH in.  The controller walks one entry per frame from
     * FRINDEX, so this must be valid before PSE is set. [USB-10] */
    for (unsigned i = 0; i < EHCI_FRAMELIST_ENTRIES; i++)
        hc->periodic[i] = EHCI_LINK_TERMINATE;
    memset(hc->intr_qh, 0, sizeof(struct ehci_qh));
    hc->intr_qh->hlink = EHCI_LINK_TERMINATE;
    hc->intr_qh->overlay_next = EHCI_LINK_TERMINATE;
    hc->intr_qh->overlay_alt_next = EHCI_LINK_TERMINATE;
    hc->intr_qh->overlay_token = EHCI_QTD_STATUS_HALTED;

    EHCI_STEP("DMA structures allocated");

    /* Program the controller. */
    ehci_op_wr(hc, EHCI_OP_CTRLDSSEG, 0);
    ehci_op_wr(hc, EHCI_OP_USBINTR, 0);              /* polling: no interrupts */
    ehci_op_wr(hc, EHCI_OP_ASYNCLIST, (uint32_t)hc->async_qh_dma);
    ehci_op_wr(hc, EHCI_OP_PERIODICLIST, (uint32_t)hc->periodic_dma);
    ehci_op_wr(hc, EHCI_OP_USBCMD,
               EHCI_CMD_RUN | EHCI_CMD_ASE | EHCI_CMD_PSE |
               EHCI_CMD_FLS_1024 | (8u << EHCI_CMD_ITC_SHIFT));
    ehci_op_wr(hc, EHCI_OP_CONFIGFLAG, EHCI_CONFIGFLAG_CF);
    EHCI_STEP("controller running");
    ehci_delay_ms(5);

    /* Power all ports on. */
    for (uint8_t p = 1; p <= hc->nports; p++) {
        uint32_t psc = ehci_portsc_rd(hc, p);
        ehci_portsc_wr(hc, p, psc | EHCI_PORT_POWER);
    }
    ehci_delay_ms(20);
    EHCI_STEP("ports powered");
    return 0;
}

/*
 * Release everything ehci_start() may have acquired.  Needed because each
 * controller is now a separate heap allocation: dropping the ehci_hc_t on a
 * failed attach would otherwise strand its DMA buffers with no pointer left
 * to free them through.
 */
static void ehci_teardown(ehci_hc_t *hc)
{
    if (hc->intr_qh)
        dma_free_coherent(hc->intr_qh, sizeof(struct ehci_qh));
    if (hc->periodic)
        dma_free_coherent(hc->periodic,
                          EHCI_FRAMELIST_ENTRIES * sizeof(uint32_t));
    if (hc->async_qh)
        dma_free_coherent(hc->async_qh, sizeof(struct ehci_qh));
    if (hc->qtd)
        dma_free_coherent(hc->qtd, EHCI_MAX_QTD * sizeof(struct ehci_qtd));
    if (hc->setup_buf)
        dma_free_coherent(hc->setup_buf, 64);
    if (hc->bounce)
        dma_free_coherent(hc->bounce, EHCI_BOUNCE_SIZE);
    if (hc->mmio)
        iounmap((void *)hc->mmio);
    kfree(hc, sizeof(*hc));
}

static int ehci_pci_attach(struct device *dev)
{
    ehci_hc_t *hc;

    pci_device_t *pdev = pci_find_device_by_kdev(dev);
    if (!pdev)
        return -1;

    /* EHCI BAR0 is an MMIO region. */
    int bt = pci_bar_type(pdev, 0);
    if (bt != PCI_BAR_MEM32 && bt != PCI_BAR_MEM64)
        return -1;
    uint32_t bar0 = pci_read_config32(pdev->bus, pdev->slot, pdev->func, 0x10);
    uintptr_t phys = bar0 & ~0xFUL;

    hc = kzalloc(sizeof(*hc));
    if (!hc) {
        kprintf("ehci: out of memory allocating controller state\n");
        return -1;
    }

    /* Enable memory space + bus mastering. */
    uint16_t cmd = pci_read_config16(pdev->bus, pdev->slot, pdev->func, PCI_CONFIG_COMMAND);
    pci_write_config16(pdev->bus, pdev->slot, pdev->func, PCI_CONFIG_COMMAND,
                       cmd | 0x0002 | 0x0004);

    hc->mmio = ioremap(phys, 0x1000);
    if (!hc->mmio) {
        kprintf("ehci: ioremap of BAR0 0x%x failed\n", (unsigned)phys);
        ehci_teardown(hc);
        return -1;
    }
    uint8_t caplen = *(volatile uint8_t *)(hc->mmio + EHCI_CAP_CAPLENGTH);
    hc->op = hc->mmio + caplen;
    uint32_t hcs = *(volatile uint32_t *)(hc->mmio + EHCI_CAP_HCSPARAMS);
    hc->nports = EHCI_HCSPARAMS_N_PORTS(hcs);
    if (hc->nports == 0) hc->nports = 1;
    hc->irq = (uint8_t)pci_get_irq(pdev);

    mutex_init(&hc->submit_lock, "ehci_submit");

    /* Own the controller before resetting it.  Defeatable from the command
     * line for the same reason as the xHCI handoff: it touches state only real
     * firmware sets up, so it has to be possible to boot without it. */
    if (!cmdline_has("nousbhandoff"))
        ehci_take_controller(hc, pdev);

    if (ehci_start(hc) != 0) {
        ehci_teardown(hc);
        return -1;
    }

    snprintf(hc->name, sizeof(hc->name), "ehci%u", ehci_instances);

    hc->hcd.priv = hc;
    hc->hcd.name = hc->name;
    hc->hcd.hcd_index = ehci_instances;
    hc->hcd.nports = hc->nports;
    hc->hcd.submit = ehci_submit;
    hc->hcd.port_status = ehci_port_status;
    hc->hcd.port_reset = ehci_port_reset;
    hc->hcd.port_enable = ehci_port_enable;

    usb_register_hcd(&hc->hcd);
    hc->initialized = 1;
    hc->kdev = dev;
    if (ehci_instances < EHCI_MAX_HCS)
        ehci_hcs[ehci_instances] = hc;   /* shutdown dispatch [ehci-audit 7] */
    ehci_instances++;
    kprintf("ehci: %s: EHCI USB 2.0 controller at 0x%x, %u ports\n",
            hc->name, (unsigned)phys, hc->nports);
    return 0;
}

/*
 * Reboot/shutdown: stop the controller's DMA and give the ports back.
 *
 * Left running, the HC keeps walking the periodic frame list and async ring
 * in THIS kernel's memory straight through a warm reboot -- the next kernel
 * reuses those pages immediately, and a controller still DMAing into them is
 * untraceable early-boot corruption.  HCRESET also reverts CONFIGFLAG and
 * port routing to power-on state (Table 2-9), so firmware/companions can
 * reclaim the ports we took in ehci_take_controller().  QEMU resets device
 * models itself; this is for real hardware. [ehci-audit 7]
 */
static void ehci_pci_shutdown(struct device *dev)
{
    for (unsigned i = 0; i < EHCI_MAX_HCS; i++) {
        ehci_hc_t *hc = ehci_hcs[i];

        if (!hc || hc->kdev != dev || !hc->initialized)
            continue;
        uint32_t cmd = ehci_op_rd(hc, EHCI_OP_USBCMD);
        ehci_op_wr(hc, EHCI_OP_USBCMD, cmd & ~EHCI_CMD_RUN);
        for (int j = 0; j < 100; j++) {
            if (ehci_op_rd(hc, EHCI_OP_USBSTS) & EHCI_STS_HCHALTED)
                break;
            ehci_delay_ms(1);
        }
        ehci_op_wr(hc, EHCI_OP_CONFIGFLAG, 0);
        ehci_op_wr(hc, EHCI_OP_USBCMD, EHCI_CMD_HCRESET);
    }
}

static const device_id_t ehci_pci_ids[] = {
    /* Any EHCI controller: class 0x0C, subclass 0x03, progif 0x20 */
    { DEVICE_ID_ANY, DEVICE_ID_ANY, 0x000C0320U, 0x00FFFFFFU, 0 },
    { 0, 0, 0, 0, 0 },
};

static struct driver ehci_pci_driver = {
    .name     = "ehci",
    .id_table = ehci_pci_ids,
    .attach   = ehci_pci_attach,
    .shutdown = ehci_pci_shutdown,
};

void ehci_init(void)
{
    if (!pci_present())
        return;
    if (cmdline_has("noehci")) {
        kprintf("ehci: disabled by noehci\n");
        return;
    }
    ehci_trace = cmdline_has("ehcidebug");
    driver_register(&ehci_pci_driver, &pci_bus_type);
}
