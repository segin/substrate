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

    struct ehci_qh   *async_qh;      dma_addr_t async_qh_dma;
    struct ehci_qtd  *qtd;           dma_addr_t qtd_dma;   /* EHCI_MAX_QTD pool */
    void             *setup_buf;     dma_addr_t setup_dma;
    void             *bounce;        dma_addr_t bounce_dma;

    char              name[8];  /* "ehciN", backs hcd.name */
    usb_hcd_t         hcd;
} ehci_hc_t;

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

/* Program one qTD: PID, byte count, toggle, buffer pointers into `bounce`. */
static void ehci_fill_qtd(ehci_hc_t *hc, int idx, uint32_t next_dma,
                          uint32_t pid, uint32_t bytes, int toggle,
                          dma_addr_t data_dma, int ioc)
{
    struct ehci_qtd *q = &hc->qtd[idx];
    memset(q, 0, sizeof(*q));
    q->next = next_dma ? next_dma : EHCI_LINK_TERMINATE;
    q->alt_next = EHCI_LINK_TERMINATE;
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
 * Stop it before the caller reclaims that memory: disable the async schedule
 * and wait for the controller to acknowledge it has stopped (ASS clears), then
 * neutralize this transfer's qTDs and the QH overlay so a late visit is a
 * no-op, and finally restart the schedule for subsequent transfers.  Without
 * this, a late completion scribbles over the next transfer's data. */
static void ehci_quiesce_async(ehci_hc_t *hc, int first_qtd, int n_qtd)
{
    uint32_t cmd = ehci_op_rd(hc, EHCI_OP_USBCMD);
    ehci_op_wr(hc, EHCI_OP_USBCMD, cmd & ~EHCI_CMD_ASE);
    for (int i = 0; i < 100; i++) {
        if (!(ehci_op_rd(hc, EHCI_OP_USBSTS) & EHCI_STS_ASS))
            break;
        ehci_delay_ms(1);
    }

    /* Schedule stopped: the HC no longer touches the QH/qTDs. */
    for (int i = first_qtd; i < first_qtd + n_qtd; i++)
        hc->qtd[i].token &= ~EHCI_QTD_STATUS_ACTIVE;
    struct ehci_qh *qh = hc->async_qh;
    qh->overlay_next = EHCI_LINK_TERMINATE;
    qh->overlay_alt_next = EHCI_LINK_TERMINATE;
    qh->overlay_token &= ~EHCI_QTD_STATUS_ACTIVE;

    /* Re-enable the async schedule for the next transfer. */
    cmd = ehci_op_rd(hc, EHCI_OP_USBCMD);
    ehci_op_wr(hc, EHCI_OP_USBCMD, cmd | EHCI_CMD_ASE);
    for (int i = 0; i < 100; i++) {
        if (ehci_op_rd(hc, EHCI_OP_USBSTS) & EHCI_STS_ASS)
            break;
        ehci_delay_ms(1);
    }
}

/* Arm the async QH at a qTD chain and poll to completion.  Returns USB_XFER_*.
 * [DRV-06] n_qtd counts the qTDs this transfer filled/linked (contiguous from
 * first_qtd); only those are polled, so stale qTDs left ACTIVE/HALTED by a prior
 * transfer (e.g. a STALLed GET_MAX_LUN) no longer poison this one. */
static int ehci_run_qh(ehci_hc_t *hc, int first_qtd, int n_qtd, uint32_t endp_char)
{
    struct ehci_qh *qh = hc->async_qh;
    qh->endp_char = endp_char | EHCI_QH_HEAD | EHCI_QH_DTC;
    qh->endp_cap  = EHCI_QH_MULT_ONE;
    qh->current_qtd = 0;
    /* Load the overlay: point at the first qTD, clear status. */
    qh->overlay_next = ehci_qtd_dma(hc, first_qtd);
    qh->overlay_alt_next = EHCI_LINK_TERMINATE;
    qh->overlay_token = 0;   /* not active/halted -> controller reloads from next */

    /* Poll the qTD chain until none is Active, or a Halt, or timeout. */
    uint64_t deadline = (uint64_t)get_uptime_ms() + EHCI_XFER_TIMEOUT_MS;
    for (;;) {
        int active = 0, halted = 0;
        for (int i = first_qtd; i < first_qtd + n_qtd; i++) {   /* [DRV-06] this xfer only */
            uint32_t tok = hc->qtd[i].token;
            /* only inspect qTDs we linked (token nonzero means we set it) */
            if (!(tok & (EHCI_QTD_STATUS_ACTIVE | EHCI_QTD_STATUS_ERRMASK)))
                continue;
            if (tok & EHCI_QTD_STATUS_ACTIVE) active = 1;
            if (tok & EHCI_QTD_STATUS_HALTED) halted = 1;
        }
        if (halted) return USB_XFER_STALL;
        if (!active) break;
        if ((uint64_t)get_uptime_ms() > deadline) {
            /* [DRV-05] Reclaim the descriptors from the hardware before the
             * caller reuses the bounce buffer / qTD pool. */
            ehci_quiesce_async(hc, first_qtd, n_qtd);
            return USB_XFER_TIMEOUT;
        }
        __asm__ volatile("pause");
    }
    return USB_XFER_OK;
}

/* Build the EHCI QH endpoint-characteristics word for this transfer. */
static uint32_t ehci_endp_char(usb_transfer_t *xfer, int is_control)
{
    usb_device_t *dev = xfer->dev;
    uint32_t addr = dev->address & 0x7F;
    uint32_t ep   = xfer->ep ? (xfer->ep->address & 0x0F) : 0;
    uint32_t mpl  = xfer->ep ? xfer->ep->max_packet : 64;
    if (mpl == 0) mpl = is_control ? 64 : 512;
    uint32_t ec = (addr << EHCI_QH_ADDR_SHIFT) |
                  (ep   << EHCI_QH_ENDPT_SHIFT) |
                  EHCI_QH_EPS_HIGH |
                  ((mpl & 0x7FF) << EHCI_QH_MPL_SHIFT);
    return ec;
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

    ehci_fill_qtd(hc, status_i, 0, status_pid, 0, 1, 0, 1);
    if (data_i >= 0) {
        uint32_t dpid = in ? EHCI_QTD_PID_IN : EHCI_QTD_PID_OUT;
        ehci_fill_qtd(hc, data_i, ehci_qtd_dma(hc, status_i), dpid, len, 1,
                      hc->bounce_dma, 0);
        ehci_fill_qtd(hc, setup_i, ehci_qtd_dma(hc, data_i),
                      EHCI_QTD_PID_SETUP, 8, 0, hc->setup_dma, 0);
    } else {
        ehci_fill_qtd(hc, setup_i, ehci_qtd_dma(hc, status_i),
                      EHCI_QTD_PID_SETUP, 8, 0, hc->setup_dma, 0);
    }

    int r = ehci_run_qh(hc, setup_i, idx, ehci_endp_char(xfer, 1)); /* [DRV-06] idx qTDs */
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
    ehci_fill_qtd(hc, 0, 0, pid, len, toggle, len ? hc->bounce_dma : 0, 1);

    int r = ehci_run_qh(hc, 0, 1, ehci_endp_char(xfer, 0)); /* [DRV-06] single qTD */
    if (r == USB_XFER_OK) {
        uint32_t residue = (hc->qtd[0].token >> EHCI_QTD_BYTES_SHIFT) & 0x7FFF;
        xfer->actual_length = len - residue;
        if (in && xfer->actual_length)
            memcpy(xfer->data, hc->bounce, xfer->actual_length);
        if (xfer->ep) {
            /* [DRV-07] One qTD moves many max-packet packets; the ending data
             * toggle is initial ^ (npackets & 1).  Advance by the parity of the
             * packets actually moved, not unconditionally by one (an even packet
             * count, e.g. 1024 B at MPS 512, must leave the toggle unchanged). */
            uint32_t mps = xfer->ep->max_packet ? xfer->ep->max_packet : 512;
            uint32_t moved = xfer->actual_length;
            uint32_t npkts = moved ? ((moved + mps - 1) / mps) : 1; /* ZLP = 1 packet */
            xfer->ep->toggle ^= (npkts & 1);
        }
    }
    xfer->status = r;
    return r;
}

static int ehci_submit(usb_hcd_t *hcd, usb_transfer_t *xfer)
{
    ehci_hc_t *hc = hcd->priv;
    int ret;
    mutex_lock(&hc->submit_lock);
    if (xfer->is_control)
        ret = ehci_control_transfer(hc, xfer);
    else if (xfer->ep && xfer->ep->type == USB_EP_TYPE_BULK)
        ret = ehci_bulk_transfer(hc, xfer);
    else if (xfer->ep && xfer->ep->type == USB_EP_TYPE_INTERRUPT)
        ret = ehci_bulk_transfer(hc, xfer);   /* single-qTD IN, same path */
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
                kprintf("ehci: waiting for the BIOS to release the controller\n");
                pci_write_config8(pdev->bus, pdev->slot, pdev->func,
                                  off + EHCI_LEGSUP_OS_SEM, 1);
                for (i = 0; i < 5000; i++) {
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
                    kprintf("ehci: BIOS never released the controller; "
                            "claiming it anyway\n");
                    pci_write_config8(pdev->bus, pdev->slot, pdev->func,
                                      off + EHCI_LEGSUP_BIOS_SEM, 0);
                }
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

static int ehci_start(ehci_hc_t *hc)
{
    if (ehci_reset_controller(hc) != 0) {
        kprintf("ehci: controller reset timeout\n");
        return -1;
    }

    /* Allocate DMA structures (once). */
    hc->async_qh = dma_alloc_coherent(sizeof(struct ehci_qh), &hc->async_qh_dma);
    hc->qtd      = dma_alloc_coherent(EHCI_MAX_QTD * sizeof(struct ehci_qtd), &hc->qtd_dma);
    hc->setup_buf = dma_alloc_coherent(64, &hc->setup_dma);
    hc->bounce    = dma_alloc_coherent(EHCI_BOUNCE_SIZE, &hc->bounce_dma);
    if (!hc->async_qh || !hc->qtd || !hc->setup_buf || !hc->bounce) {
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

    /* Program the controller. */
    ehci_op_wr(hc, EHCI_OP_CTRLDSSEG, 0);
    ehci_op_wr(hc, EHCI_OP_USBINTR, 0);              /* polling: no interrupts */
    ehci_op_wr(hc, EHCI_OP_ASYNCLIST, (uint32_t)hc->async_qh_dma);
    ehci_op_wr(hc, EHCI_OP_PERIODICLIST, 0);
    ehci_op_wr(hc, EHCI_OP_USBCMD,
               EHCI_CMD_RUN | EHCI_CMD_ASE | (8u << EHCI_CMD_ITC_SHIFT));
    ehci_op_wr(hc, EHCI_OP_CONFIGFLAG, EHCI_CONFIGFLAG_CF);
    ehci_delay_ms(5);

    /* Power all ports on. */
    for (uint8_t p = 1; p <= hc->nports; p++) {
        uint32_t psc = ehci_portsc_rd(hc, p);
        ehci_portsc_wr(hc, p, psc | EHCI_PORT_POWER);
    }
    ehci_delay_ms(20);
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

    /* Own the controller before resetting it. */
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
    ehci_instances++;
    kprintf("ehci: %s: EHCI USB 2.0 controller at 0x%x, %u ports\n",
            hc->name, (unsigned)phys, hc->nports);
    return 0;
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
};

void ehci_init(void)
{
    if (!pci_present())
        return;
    driver_register(&ehci_pci_driver, &pci_bus_type);
}
