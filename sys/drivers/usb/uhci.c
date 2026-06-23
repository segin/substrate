/*
 * uhci.c - UHCI Host Controller Driver
 *
 * Universal Host Controller Interface (Intel) for USB 1.1.
 * I/O-port--based, polling-mode driver (QEMU default USB HC).
 *
 * This driver implements:
 *   - PCI probe and HC initialization
 *   - Frame list and schedule setup
 *   - TD/QH pool management
 *   - Control and bulk transfer execution (synchronous, polling)
 *   - Root hub port status and reset
 */

#include "uhci.h"
#include "usb.h"
#include <kern/pci.h>
#include <kern/driver.h>
#include <kern/bus.h>
#include <kern/time.h>
#include <kern/sched.h>
#include <kern/console.h>
#include <sys/dma.h>
#include <sys/irq.h>
#include <sys/lock.h>
#include <intr.h>
#include <vm/vm_kmem.h>
#include <string.h>

#include <io.h>

/*
 * ============================================================
 * UHCI Controller State
 * ============================================================
 */

typedef struct uhci_hc {
    uint16_t iobase;            /* I/O base address */
    uint8_t  irq;               /* IRQ line */

    /* Frame list (1024 entries, 4KB aligned, physical pointers) */
    uint32_t    *frame_list;
    dma_addr_t   frame_list_dma;

    /* TD pool.  td_alloc_hint is a rotating cursor: a fresh search starts
     * here and wraps.  Without it the linear scan was O(N²) per transfer
     * — a 64 KB bulk transfer needing 1024 TDs scanned 1+2+...+1024 ≈
     * 524 K slots, and a multi-MB read repeated that for every chunk. */
    struct uhci_td *td_pool;
    dma_addr_t      td_pool_dma;
    uint8_t         td_used[UHCI_MAX_TDS];
    uint32_t        td_alloc_hint;

    /* QH pool */
    struct uhci_qh *qh_pool;
    dma_addr_t      qh_pool_dma;
    uint8_t         qh_used[UHCI_MAX_QHS];

    /* Skeleton QH for async (bulk/control) transfers */
    struct uhci_qh *async_qh;
    dma_addr_t      async_qh_dma;

    /* Pre-allocated 8-byte DMA buffer for control SETUP packets.
     * Reused across every control transfer so we don't pay
     * dma_alloc_coherent (which rounds up to a full 4KB page) per request.
     * Safe under submit_lock since we serialize all transfers anyway. */
    uint8_t        *setup_buf;
    dma_addr_t      setup_buf_dma;

    /* Serializes access to the shared async schedule and TD/QH pools. */
    mutex_t submit_lock;

    /* USB HCD handle */
    usb_hcd_t hcd;
} uhci_hc_t;

static uhci_hc_t uhci_ctrl;
static int uhci_initialized;

/*
 * ============================================================
 * Register Access
 * ============================================================
 */

static inline void uhci_writew(uhci_hc_t *hc, uint16_t reg, uint16_t val)
{
    outw(hc->iobase + reg, val);
}

static inline uint16_t uhci_readw(uhci_hc_t *hc, uint16_t reg)
{
    return inw(hc->iobase + reg);
}

static inline void uhci_writel(uhci_hc_t *hc, uint16_t reg, uint32_t val)
{
    outl(hc->iobase + reg, val);
}

/*
 * ============================================================
 * TD/QH Pool Management
 * ============================================================
 */

static struct uhci_td *uhci_alloc_td(uhci_hc_t *hc, dma_addr_t *phys)
{
    /* Search from the hint, wrap once.  After freeing a chunk's worth
     * of TDs the next allocation typically finds a free slot in O(1)
     * because the hint is already past the previously-used range. */
    uint32_t start = hc->td_alloc_hint;
    for (uint32_t step = 0; step < UHCI_MAX_TDS; step++) {
        uint32_t i = start + step;
        if (i >= UHCI_MAX_TDS) i -= UHCI_MAX_TDS;
        if (!hc->td_used[i]) {
            hc->td_used[i] = 1;
            hc->td_alloc_hint = (i + 1U) % UHCI_MAX_TDS;
            struct uhci_td *td = &hc->td_pool[i];
            memset(td, 0, sizeof(*td));
            /* Initialise link to terminate (T bit set).  memset alone
             * leaves link=0, which the cleanup walker treats as a
             * forward pointer, dereferencing 0xC0000000 if a transfer
             * setup aborts before the chain is wired up. */
            td->link = UHCI_TD_LINK_T;
            *phys = hc->td_pool_dma + (dma_addr_t)(i * sizeof(struct uhci_td));
            return td;
        }
    }
    return NULL;
}

static void uhci_free_td(uhci_hc_t *hc, struct uhci_td *td)
{
    int idx = (int)(td - hc->td_pool);
    if (idx >= 0 && idx < UHCI_MAX_TDS)
        hc->td_used[idx] = 0;
}

static struct uhci_qh *uhci_alloc_qh(uhci_hc_t *hc, dma_addr_t *phys)
{
    for (int i = 0; i < UHCI_MAX_QHS; i++) {
        if (!hc->qh_used[i]) {
            hc->qh_used[i] = 1;
            struct uhci_qh *qh = &hc->qh_pool[i];
            memset(qh, 0, sizeof(*qh));
            *phys = hc->qh_pool_dma + (dma_addr_t)(i * sizeof(struct uhci_qh));
            return qh;
        }
    }
    return NULL;
}

static void uhci_free_qh(uhci_hc_t *hc, struct uhci_qh *qh)
    __attribute__((unused));
static void uhci_free_qh(uhci_hc_t *hc, struct uhci_qh *qh)
{
    int idx = (int)(qh - hc->qh_pool);
    if (idx >= 0 && idx < UHCI_MAX_QHS)
        hc->qh_used[idx] = 0;
}

/*
 * ============================================================
 * HC Initialization
 * ============================================================
 */

static int uhci_reset(uhci_hc_t *hc)
{
    uint64_t deadline;

    /* Global reset (drives USB bus reset for 10ms) */
    uhci_writew(hc, UHCI_USBCMD, UHCI_CMD_GRESET);
    deadline = (uint64_t)get_uptime_ms() + 50;
    while ((uint64_t)get_uptime_ms() < deadline)
        __asm__ volatile("pause");
    uhci_writew(hc, UHCI_USBCMD, 0);

    /* HC reset */
    uhci_writew(hc, UHCI_USBCMD, UHCI_CMD_HCRESET);
    deadline = (uint64_t)get_uptime_ms() + 100;
    while (uhci_readw(hc, UHCI_USBCMD) & UHCI_CMD_HCRESET) {
        if ((uint64_t)get_uptime_ms() > deadline) {
            kprintf("uhci: HC reset timeout\n");
            return -1;
        }
        __asm__ volatile("pause");
    }

    return 0;
}

static int uhci_alloc_structures(uhci_hc_t *hc)
{
    /* Frame list: 1024 x 4 bytes = 4KB, 4KB-aligned */
    hc->frame_list = dma_alloc_coherent(UHCI_FRAME_LIST_SIZE * sizeof(uint32_t),
                                        &hc->frame_list_dma);
    if (!hc->frame_list)
        return -1;

    /* TD pool */
    hc->td_pool = dma_alloc_coherent(UHCI_MAX_TDS * sizeof(struct uhci_td),
                                     &hc->td_pool_dma);
    if (!hc->td_pool) {
        dma_free_coherent(hc->frame_list, UHCI_FRAME_LIST_SIZE * sizeof(uint32_t));
        return -1;
    }
    memset(hc->td_used, 0, sizeof(hc->td_used));

    /* QH pool */
    hc->qh_pool = dma_alloc_coherent(UHCI_MAX_QHS * sizeof(struct uhci_qh),
                                     &hc->qh_pool_dma);
    if (!hc->qh_pool) {
        dma_free_coherent(hc->td_pool, UHCI_MAX_TDS * sizeof(struct uhci_td));
        dma_free_coherent(hc->frame_list, UHCI_FRAME_LIST_SIZE * sizeof(uint32_t));
        return -1;
    }
    memset(hc->qh_used, 0, sizeof(hc->qh_used));

    hc->td_alloc_hint = 0;

    /* Single 8-byte DMA buffer for control SETUP packets (reused). */
    hc->setup_buf = dma_alloc_coherent(8, &hc->setup_buf_dma);
    if (!hc->setup_buf) {
        dma_free_coherent(hc->qh_pool, UHCI_MAX_QHS * sizeof(struct uhci_qh));
        dma_free_coherent(hc->td_pool, UHCI_MAX_TDS * sizeof(struct uhci_td));
        dma_free_coherent(hc->frame_list, UHCI_FRAME_LIST_SIZE * sizeof(uint32_t));
        return -1;
    }

    return 0;
}

static void uhci_setup_schedule(uhci_hc_t *hc)
{
    dma_addr_t async_phys;

    /* Allocate the async QH (anchor for control/bulk transfers) */
    hc->async_qh = uhci_alloc_qh(hc, &async_phys);
    hc->async_qh_dma = async_phys;

    /* Async QH: terminates both horizontal and vertical links */
    hc->async_qh->head_link = UHCI_QH_LINK_T;
    hc->async_qh->element_link = UHCI_QH_LINK_T;

    /* Point every frame list entry at the async QH */
    for (int i = 0; i < UHCI_FRAME_LIST_SIZE; i++) {
        hc->frame_list[i] = (uint32_t)async_phys | UHCI_TD_LINK_QH;
    }
}

static void uhci_start(uhci_hc_t *hc)
{
    /* Set frame list base address */
    uhci_writel(hc, UHCI_FLBASEADD, (uint32_t)hc->frame_list_dma);

    /* Start at frame 0 */
    uhci_writew(hc, UHCI_FRNUM, 0);

    /* Clear status bits */
    uhci_writew(hc, UHCI_USBSTS, 0xFFFF);

    /* Disable interrupts (we use polling) */
    uhci_writew(hc, UHCI_USBINTR, 0);

    /* Start the HC: Run + 64-byte max packet */
    uhci_writew(hc, UHCI_USBCMD, UHCI_CMD_RS | UHCI_CMD_CF | UHCI_CMD_MAXP);
}

/*
 * ============================================================
 * Root Hub Port Operations
 * ============================================================
 */

static uint16_t uhci_portsc_reg(uint8_t port)
{
    /* Ports are 1-indexed */
    return (port == 1) ? UHCI_PORTSC1 : UHCI_PORTSC2;
}

static uint32_t uhci_port_status(usb_hcd_t *hcd, uint8_t port)
{
    uhci_hc_t *hc = hcd->priv;
    uint16_t portsc;
    uint32_t status = 0;

    if (port < 1 || port > UHCI_NUM_PORTS)
        return 0;

    portsc = uhci_readw(hc, uhci_portsc_reg(port));

    if (portsc & UHCI_PORTSC_CCS)
        status |= USB_PORT_STAT_CONNECTION;
    if (portsc & UHCI_PORTSC_PE)
        status |= USB_PORT_STAT_ENABLE;
    if (portsc & UHCI_PORTSC_PR)
        status |= USB_PORT_STAT_RESET;
    if (portsc & UHCI_PORTSC_LSDA)
        status |= USB_PORT_STAT_LOW_SPEED;

    /* Always report powered (UHCI has no per-port power control) */
    status |= USB_PORT_STAT_POWER;

    /* Change bits in upper 16 */
    if (portsc & UHCI_PORTSC_CSC)
        status |= (uint32_t)USB_PORT_STAT_C_CONNECTION << 16;

    return status;
}

static int uhci_port_reset(usb_hcd_t *hcd, uint8_t port)
{
    uhci_hc_t *hc = hcd->priv;
    uint16_t reg;
    uint16_t portsc;
    uint64_t deadline;

    /* CSC and PEC are write-1-to-clear: blindly OR-ing into PORTSC
     * accidentally clears any pending change bits the next layer
     * still needs to see (= missed hot-plug events).  Mask them out
     * before every modify. */
    const uint16_t W1C = UHCI_PORTSC_CSC | UHCI_PORTSC_PEC;

    if (port < 1 || port > UHCI_NUM_PORTS)
        return -1;

    reg = uhci_portsc_reg(port);

    /* Assert reset */
    portsc = uhci_readw(hc, reg) & ~W1C;
    uhci_writew(hc, reg, portsc | UHCI_PORTSC_PR);

    /* Hold reset for 50ms (USB spec minimum 10ms, UHCI recommends 50ms) */
    deadline = (uint64_t)get_uptime_ms() + 50;
    while ((uint64_t)get_uptime_ms() < deadline)
        __asm__ volatile("pause");

    /* Deassert reset */
    portsc = uhci_readw(hc, reg) & ~W1C;
    uhci_writew(hc, reg, portsc & ~UHCI_PORTSC_PR);

    /* Wait for reset to complete and port to stabilize */
    deadline = (uint64_t)get_uptime_ms() + 100;
    while ((uint64_t)get_uptime_ms() < deadline)
        __asm__ volatile("pause");

    /* Enable port */
    portsc = uhci_readw(hc, reg) & ~W1C;
    uhci_writew(hc, reg, portsc | UHCI_PORTSC_PE);

    /* Clear status change bits (write-1-to-clear) */
    portsc = uhci_readw(hc, reg);
    uhci_writew(hc, reg, portsc | UHCI_PORTSC_CSC | UHCI_PORTSC_PEC);

    /* Verify port is enabled */
    portsc = uhci_readw(hc, reg);
    if (!(portsc & UHCI_PORTSC_PE)) {
        /* Try enabling again — same masking */
        uhci_writew(hc, reg, (portsc & ~W1C) | UHCI_PORTSC_PE);
        deadline = (uint64_t)get_uptime_ms() + 50;
        while ((uint64_t)get_uptime_ms() < deadline)
            __asm__ volatile("pause");
    }

    return 0;
}

static int uhci_port_enable(usb_hcd_t *hcd, uint8_t port, int enable)
{
    uhci_hc_t *hc = hcd->priv;
    uint16_t reg;
    uint16_t portsc;

    if (port < 1 || port > UHCI_NUM_PORTS)
        return -1;

    reg = uhci_portsc_reg(port);
    portsc = uhci_readw(hc, reg);

    if (enable)
        portsc |= UHCI_PORTSC_PE;
    else
        portsc &= ~UHCI_PORTSC_PE;

    uhci_writew(hc, reg, portsc);
    return 0;
}

/*
 * ============================================================
 * Transfer Execution (Synchronous Polling)
 * ============================================================
 */

/*
 * Wait for a TD chain to complete by polling the Active bit.
 * Returns 0 on success, negative on error/timeout.
 */
static int uhci_poll_td(uhci_hc_t *hc, struct uhci_td *td,
                        uint32_t timeout_ms, uint32_t *actual_len)
{
    uint64_t deadline = (uint64_t)get_uptime_ms() + timeout_ms;
    uint32_t total = 0;
    /* Cap the chain walk at the TD pool size — a corrupt link pointing
     * back into already-walked TDs would otherwise loop forever. */
    uint32_t walk_budget = UHCI_MAX_TDS;

    (void)hc;

    while (td && walk_budget--) {
        /* Poll until this TD is no longer active.  Run the wait with
         * interrupts ENABLED so the timer tick can preempt us.  The HID poll
         * kthread runs with IF=0 (switch_to doesn't save/restore EFLAGS), so a
         * bare pause-spin here is never preempted: a wedged transfer would
         * monopolise the CPU for the full 5 s timeout and freeze the whole UI
         * (opening dtcm reproduced it).  Enabling interrupts lets the X server
         * and its clients keep running while the controller works. */
        uint32_t _saved_if = intr_disable();
        intr_enable();
        /* Tight-spin a short while to catch a fast completion at low latency,
         * then yield the CPU.  A slow or wedged transfer — e.g. a HID GET_REPORT
         * poll that NAKs while the device is idle — would otherwise pause-spin
         * for the whole timeout, burning a core as kernel CPU and starving the
         * desktop (two HID poll kthreads doing this is most of the "slow but
         * low user-CPU" stutter).  Interrupts are enabled above and sched_yield()
         * saves/restores the caller's IF, so yielding stays preemptible and
         * IF-safe from the IF=0 poll kthread; the controller completes the
         * transfer in the background either way. */
        unsigned _spins = 0;
        while (td->ctrl_status & UHCI_TD_CTRL_ACTIVE) {
            if ((uint64_t)get_uptime_ms() > deadline) {
                intr_restore(_saved_if);
                return USB_XFER_TIMEOUT;
            }
            if (_spins < UHCI_POLL_SPIN_LIMIT) {
                _spins++;
                __asm__ volatile("pause");
            } else {
                sched_yield();
            }
        }
        intr_restore(_saved_if);

        /* Check for errors */
        if (td->ctrl_status & UHCI_TD_CTRL_STALLED) {
            // kprintf("uhci: TD stall (token=0x%08x)\n", td->token);
            return USB_XFER_STALL;
        }
        if (td->ctrl_status & (UHCI_TD_CTRL_DBUFERR | UHCI_TD_CTRL_BABBLE |
                                UHCI_TD_CTRL_CRCTMO | UHCI_TD_CTRL_BITSTUFF)) {
            kprintf("uhci: TD error 0x%08x (token=0x%08x)\n", 
                    td->ctrl_status & UHCI_TD_CTRL_ERRMASK, td->token);
            return USB_XFER_ERROR;
        }

        uint32_t actlen = (td->ctrl_status & UHCI_TD_ACTLEN_MASK);

        /* Accumulate actual transfer length (payload only, skip SETUP/STATUS overhead) */
        uint8_t pid = td->token & 0xFF;
        if (pid == UHCI_TD_PID_IN || pid == UHCI_TD_PID_OUT) {
            if (actlen != UHCI_TD_ACTLEN_NULL)
                total += actlen + 1;
        }

        /* Check for short packet (less than max expected) */
        uint32_t maxlen = ((td->token >> 21) & 0x7FF);
        uint32_t expected = (maxlen == 0x7FF) ? 0 : maxlen + 1;
        uint32_t actual_bytes = (actlen == 0x7FF) ? 0 : actlen + 1;

        if (actual_bytes < expected) {
            /* Short packet — stop here */
            // kprintf("uhci: short packet (%u < %u)\n", actual_bytes, expected);
            break;
        }

        /* Move to next TD */
        if (td->link & UHCI_TD_LINK_T)
            break;
        td = (struct uhci_td *)((uintptr_t)(td->link & ~0xF) + 0xC0000000);
    }

    if (actual_len)
        *actual_len = total;

    return USB_XFER_OK;
}

/*
 * Execute a control transfer: SETUP → DATA (optional) → STATUS
 */
static int uhci_control_transfer(uhci_hc_t *hc, usb_transfer_t *xfer)
{
    struct uhci_td *setup_td, *data_td, *status_td;
    struct uhci_td *first_td, *prev_td;
    dma_addr_t setup_phys, status_phys;
    dma_addr_t data_buf_dma;
    uint8_t addr, ep_num;
    uint8_t toggle;
    int is_in;
    uint32_t actual = 0;
    int ret;

    addr = xfer->dev->address;
    ep_num = xfer->ep->address & USB_EP_NUM_MASK;
    is_in = (xfer->setup.bmRequestType & USB_DIR_IN) ? 1 : 0;

    /* Use the pre-allocated per-HC setup buffer (covered by submit_lock). */
    memcpy(hc->setup_buf, &xfer->setup, 8);

    /* Setup TD: always DATA0 */
    setup_td = uhci_alloc_td(hc, &setup_phys);
    if (!setup_td)
        return USB_XFER_ERROR;

    setup_td->ctrl_status = UHCI_TD_CTRL_ACTIVE |
                            (3U << UHCI_TD_CTRL_CERR_SHIFT) |
                            ((xfer->dev->speed == USB_SPEED_LOW) ? UHCI_TD_CTRL_LS : 0);
    setup_td->token = UHCI_TD_TOKEN(UHCI_TD_PID_SETUP, addr, ep_num, 0, 8);
    setup_td->buffer = (uint32_t)hc->setup_buf_dma;

    first_td = setup_td;
    prev_td = setup_td;
    toggle = 1; /* Data phase starts with DATA1 */

    /* Data TDs (if any) */
    data_td = NULL;
    data_buf_dma = 0;
    if (xfer->data && xfer->length > 0) {
        data_buf_dma = dma_map_single(xfer->data, xfer->length,
                                      is_in ? DMA_FROM_DEVICE : DMA_TO_DEVICE);

        uint32_t remaining = xfer->length;
        uint32_t offset = 0;
        uint16_t max_pkt = xfer->ep->max_packet;
        if (max_pkt == 0) max_pkt = 8;

        while (remaining > 0) {
            dma_addr_t td_phys;
            uint32_t chunk = (remaining > max_pkt) ? max_pkt : remaining;

            data_td = uhci_alloc_td(hc, &td_phys);
            if (!data_td) {
                ret = USB_XFER_ERROR;
                goto cleanup;
            }

            data_td->ctrl_status = UHCI_TD_CTRL_ACTIVE |
                                   (3U << UHCI_TD_CTRL_CERR_SHIFT) |
                                   UHCI_TD_CTRL_SPD |
                                   ((xfer->dev->speed == USB_SPEED_LOW) ? UHCI_TD_CTRL_LS : 0);
            data_td->token = UHCI_TD_TOKEN(
                is_in ? UHCI_TD_PID_IN : UHCI_TD_PID_OUT,
                addr, ep_num, toggle, chunk);
            data_td->buffer = (uint32_t)(data_buf_dma + offset);

            prev_td->link = (uint32_t)td_phys | UHCI_TD_LINK_VF;
            prev_td = data_td;

            toggle ^= 1;
            offset += chunk;
            remaining -= chunk;
        }
    }

    /* Status TD: opposite direction of data, DATA1 */
    status_td = uhci_alloc_td(hc, &status_phys);
    if (!status_td) {
        ret = USB_XFER_ERROR;
        goto cleanup;
    }

    status_td->ctrl_status = UHCI_TD_CTRL_ACTIVE | UHCI_TD_CTRL_IOC |
                             (3U << UHCI_TD_CTRL_CERR_SHIFT) |
                             ((xfer->dev->speed == USB_SPEED_LOW) ? UHCI_TD_CTRL_LS : 0);
    /*
     * Status phase uses opposite direction of data/setup, always DATA1.
     * IN transfer (device→host data): status = OUT (host→device ack)
     * OUT or no-data transfer:        status = IN  (device→host ack)
     */
    if (is_in) {
        status_td->token = UHCI_TD_TOKEN_ZERO(UHCI_TD_PID_OUT, addr, ep_num, 1);
    } else {
        status_td->token = UHCI_TD_TOKEN_ZERO(UHCI_TD_PID_IN, addr, ep_num, 1);
    }
    status_td->buffer = 0;
    status_td->link = UHCI_TD_LINK_T;

    prev_td->link = (uint32_t)status_phys | UHCI_TD_LINK_VF;

    /* Insert into async QH */
    hc->async_qh->element_link = (uint32_t)setup_phys;

    /* Poll for completion */
    ret = uhci_poll_td(hc, setup_td, 5000, &actual);

    /* Remove from schedule */
    hc->async_qh->element_link = UHCI_QH_LINK_T;

    xfer->actual_length = actual;
    xfer->status = ret;

cleanup:
    /* Free all TDs by walking from first_td */
    {
        struct uhci_td *td = first_td;
        while (td) {
            struct uhci_td *next = NULL;
            if (!(td->link & UHCI_TD_LINK_T))
                next = (struct uhci_td *)((uintptr_t)(td->link & ~0xF) + 0xC0000000);
            uhci_free_td(hc, td);
            td = next;
        }
    }

    if (data_buf_dma)
        dma_unmap_single(data_buf_dma, xfer->length,
                         is_in ? DMA_FROM_DEVICE : DMA_TO_DEVICE);

    return ret;
}

/*
 * Execute a bulk transfer on a non-control endpoint.
 */
static int uhci_bulk_transfer(uhci_hc_t *hc, usb_transfer_t *xfer)
{
    struct uhci_td *first_td = NULL, *prev_td = NULL;
    dma_addr_t first_phys = 0;
    dma_addr_t data_dma;
    uint8_t addr, ep_num, is_in;
    uint16_t max_pkt;
    uint32_t remaining, offset;
    uint32_t actual = 0;
    int ret;

    addr = xfer->dev->address;
    ep_num = xfer->ep->address & USB_EP_NUM_MASK;
    is_in = (xfer->ep->address & USB_EP_DIR_MASK) ? 1 : 0;
    max_pkt = xfer->ep->max_packet;
    if (max_pkt == 0) max_pkt = 64;

    if (!xfer->data || xfer->length == 0)
        return USB_XFER_ERROR;

    data_dma = dma_map_single(xfer->data, xfer->length,
                              is_in ? DMA_FROM_DEVICE : DMA_TO_DEVICE);

    remaining = xfer->length;
    offset = 0;

    uint8_t current_toggle = xfer->ep->toggle;

    // kprintf("uhci: bulk %s addr=%u ep=%u len=%u maxp=%u toggle=%d\n",
    //         is_in ? "IN" : "OUT", addr, ep_num, xfer->length, max_pkt, current_toggle);

    while (remaining > 0) {
        struct uhci_td *td;
        dma_addr_t td_phys;
        uint32_t chunk = (remaining > max_pkt) ? max_pkt : remaining;

        td = uhci_alloc_td(hc, &td_phys);
        if (!td) {
            ret = USB_XFER_ERROR;
            goto cleanup;
        }

        td->ctrl_status = UHCI_TD_CTRL_ACTIVE |
                          (3U << UHCI_TD_CTRL_CERR_SHIFT) |
                          (is_in ? UHCI_TD_CTRL_SPD : 0) |
                          ((xfer->dev->speed == USB_SPEED_LOW) ? UHCI_TD_CTRL_LS : 0);
        td->token = UHCI_TD_TOKEN(
            is_in ? UHCI_TD_PID_IN : UHCI_TD_PID_OUT,
            addr, ep_num, current_toggle, chunk);
        td->buffer = (uint32_t)(data_dma + offset);
        td->link = UHCI_TD_LINK_T;

        current_toggle ^= 1;

        if (!first_td) {
            first_td = td;
            first_phys = td_phys;
        }
        if (prev_td)
            prev_td->link = (uint32_t)td_phys | UHCI_TD_LINK_VF;

        prev_td = td;
        offset += chunk;
        remaining -= chunk;
    }

    /* Mark last TD with IOC */
    if (prev_td)
        prev_td->ctrl_status |= UHCI_TD_CTRL_IOC;

    /* Insert into async QH */
    hc->async_qh->element_link = (uint32_t)first_phys;

    /* Poll for completion */
    ret = uhci_poll_td(hc, first_td, 5000, &actual);

    /* Remove from schedule */
    hc->async_qh->element_link = UHCI_QH_LINK_T;

    xfer->actual_length = actual;
    xfer->status = ret;

    /* Update endpoint toggle based on actual packets transferred */
    if (ret == USB_XFER_OK || ret == USB_XFER_SHORT || ret == USB_XFER_STALL) {
        struct uhci_td *td = first_td;
        while (td) {
            /* If this TD completed (Active bit cleared), it consumed a toggle */
            if (!(td->ctrl_status & UHCI_TD_CTRL_ACTIVE)) {
                xfer->ep->toggle ^= 1;
                
                /* If it was a short packet, the hardware stopped here */
                uint32_t actlen = (td->ctrl_status & UHCI_TD_ACTLEN_MASK);
                uint32_t maxlen = ((td->token >> 21) & 0x7FF);
                if (actlen != maxlen) break;
            } else {
                /* TD still active, hardware hasn't reached it */
                break;
            }

            if (td->link & UHCI_TD_LINK_T) break;
            td = (struct uhci_td *)((uintptr_t)(td->link & ~0xF) + 0xC0000000);
        }
    }

cleanup:
    /* Free all TDs */
    {
        struct uhci_td *td = first_td;
        while (td) {
            struct uhci_td *next = NULL;
            if (!(td->link & UHCI_TD_LINK_T))
                next = (struct uhci_td *)((uintptr_t)(td->link & ~0xF) + 0xC0000000);
            uhci_free_td(hc, td);
            td = next;
        }
    }

    dma_unmap_single(data_dma, xfer->length,
                     is_in ? DMA_FROM_DEVICE : DMA_TO_DEVICE);

    return ret;
}

/*
 * Isochronous OUT transfer (synchronous, polling) — USB audio playback.
 *
 * A full-speed isochronous OUT endpoint takes one packet (<= wMaxPacketSize)
 * per 1ms frame, with no handshake and no retry.  We schedule one iso TD per
 * frame directly into the frame list (each TD links onward to the async QH so
 * control/bulk keep running), a couple of frames ahead of the controller's
 * current FRNUM, then poll until the controller has retired the batch and
 * restore the frame-list slots.  Large buffers are split into batches that
 * never wrap the 1024-entry frame list into the slot being executed.
 */
#define UHCI_ISO_LEAD    2U     /* schedule this many frames ahead of FRNUM */
#define UHCI_ISO_BATCH   200U   /* max one-packet frames per poll cycle    */

static int uhci_iso_out_transfer(uhci_hc_t *hc, usb_transfer_t *xfer)
{
    uint8_t    addr    = xfer->dev->address;
    uint8_t    ep_num  = xfer->ep->address & USB_EP_NUM_MASK;
    uint16_t   max_pkt = xfer->ep->max_packet ? xfer->ep->max_packet : 192;
    uint32_t   async   = (uint32_t)hc->async_qh_dma | UHCI_TD_LINK_QH;
    dma_addr_t data_dma;
    uint32_t   off = 0;
    int        result = USB_XFER_OK;

    if ((xfer->ep->address & USB_EP_DIR_MASK) != USB_EP_DIR_OUT)
        return USB_XFER_ERROR;      /* iso IN (capture) not implemented yet */
    if (!xfer->data || xfer->length == 0)
        return USB_XFER_ERROR;

    data_dma = dma_map_single(xfer->data, xfer->length, DMA_TO_DEVICE);

    while (off < xfer->length && result == USB_XFER_OK) {
        struct uhci_td *tds[UHCI_ISO_BATCH];
        dma_addr_t      tphys[UHCI_ISO_BATCH];
        uint16_t        frames[UHCI_ISO_BATCH];
        uint32_t        npkts = 0;
        uint32_t        batch_bytes = 0;
        uint16_t        start;
        uint64_t        deadline;

        /* Carve a batch of one-packet-per-frame iso TDs from the buffer. */
        while (off + batch_bytes < xfer->length && npkts < UHCI_ISO_BATCH) {
            uint32_t rem   = xfer->length - off - batch_bytes;
            uint32_t chunk = rem > max_pkt ? max_pkt : rem;
            dma_addr_t tp;
            struct uhci_td *td = uhci_alloc_td(hc, &tp);
            if (!td)
                break;
            td->ctrl_status = UHCI_TD_CTRL_IOS | UHCI_TD_CTRL_ACTIVE;
            td->token  = UHCI_TD_TOKEN(UHCI_TD_PID_OUT, addr, ep_num, 0, chunk);
            td->buffer = (uint32_t)(data_dma + off + batch_bytes);
            td->link   = async;     /* run the async schedule after this TD */
            tds[npkts]   = td;
            tphys[npkts] = tp;
            npkts++;
            batch_bytes += chunk;
        }
        if (npkts == 0) {           /* TD pool exhausted */
            result = USB_XFER_ERROR;
            break;
        }

        /* Schedule into frames LEAD ahead of the controller's position. */
        start = (uint16_t)((uhci_readw(hc, UHCI_FRNUM) + UHCI_ISO_LEAD)
                           & (UHCI_FRAME_LIST_SIZE - 1));
        for (uint32_t i = 0; i < npkts; i++)
            frames[i] = (uint16_t)((start + i) & (UHCI_FRAME_LIST_SIZE - 1));

        __sync_synchronize();       /* TD contents visible before linking */
        for (uint32_t i = 0; i < npkts; i++)
            hc->frame_list[frames[i]] = (uint32_t)tphys[i];   /* TD, not QH */

        /* Poll until the controller retires the last frame (one packet per
         * millisecond, so npkts ms plus generous slack).  Interrupts enabled
         * + spin-then-yield, matching uhci_poll_td so we stay preemptible. */
        deadline = (uint64_t)get_uptime_ms() + npkts + 50U;
        {
            uint32_t saved_if = intr_disable();
            unsigned spins = 0;
            intr_enable();
            while (tds[npkts - 1]->ctrl_status & UHCI_TD_CTRL_ACTIVE) {
                if ((uint64_t)get_uptime_ms() > deadline) {
                    result = USB_XFER_TIMEOUT;
                    break;
                }
                if (spins < UHCI_POLL_SPIN_LIMIT) {
                    spins++;
                    __asm__ volatile("pause");
                } else {
                    sched_yield();
                }
            }
            intr_restore(saved_if);
        }

        /* Unlink from the frame list and reclaim the TDs. */
        for (uint32_t i = 0; i < npkts; i++) {
            hc->frame_list[frames[i]] = async;
            uhci_free_td(hc, tds[i]);
        }
        __sync_synchronize();

        off += batch_bytes;
    }

    dma_unmap_single(data_dma, xfer->length, DMA_TO_DEVICE);

    xfer->actual_length = off;
    xfer->status = result;
    return result;
}

/*
 * HCD submit callback — dispatches to control, bulk, or isochronous handler.
 */
static int uhci_submit(usb_hcd_t *hcd, usb_transfer_t *xfer)
{
    uhci_hc_t *hc = hcd->priv;
    int ret;

    mutex_lock(&hc->submit_lock);
    if (xfer->is_control) {
        ret = uhci_control_transfer(hc, xfer);
    } else if (xfer->ep->type == USB_EP_TYPE_BULK) {
        ret = uhci_bulk_transfer(hc, xfer);
    } else if (xfer->ep->type == USB_EP_TYPE_ISO) {
        ret = uhci_iso_out_transfer(hc, xfer);
    } else {
        kprintf("uhci: unsupported transfer type %u\n", xfer->ep->type);
        ret = USB_XFER_ERROR;
    }
    mutex_unlock(&hc->submit_lock);
    return ret;
}

/*
 * ============================================================
 * PCI Probe & Initialization
 * ============================================================
 */

static int uhci_pci_attach(struct device *dev)
{
    pci_device_t *pdev;
    uint16_t cmd;
    uint32_t bar4;

    if (uhci_initialized)
        return -1;  /* Only support one UHCI controller */

    pdev = pci_find_device_by_kdev(dev);
    if (!pdev)
        return -1;

    /* UHCI uses BAR4 for I/O base address */
    if (pci_bar_type(pdev, 4) != PCI_BAR_IO)
        return -1;

    bar4 = pci_read_config32(pdev->bus, pdev->slot, pdev->func, 0x20);
    uhci_ctrl.iobase = (uint16_t)(bar4 & ~0x1F);

    /* Enable I/O space and bus mastering */
    cmd = pci_read_config16(pdev->bus, pdev->slot, pdev->func, PCI_CONFIG_COMMAND);
    pci_write_config16(pdev->bus, pdev->slot, pdev->func, PCI_CONFIG_COMMAND,
                       cmd | PCI_COMMAND_IO | PCI_COMMAND_MASTER);

    uhci_ctrl.irq = (uint8_t)pci_get_irq(pdev);
    mutex_init(&uhci_ctrl.submit_lock, "uhci_submit");

    kprintf("uhci: PCI %02x:%02x.%x iobase=0x%04x irq=%u\n",
            pdev->bus, pdev->slot, pdev->func,
            uhci_ctrl.iobase, uhci_ctrl.irq);

    /* Reset the HC */
    if (uhci_reset(&uhci_ctrl) < 0)
        return -1;

    /* Allocate DMA structures */
    if (uhci_alloc_structures(&uhci_ctrl) < 0)
        return -1;

    /* Setup schedule */
    uhci_setup_schedule(&uhci_ctrl);

    /* Start the HC */
    uhci_start(&uhci_ctrl);

    /* Verify HC is running */
    if (uhci_readw(&uhci_ctrl, UHCI_USBSTS) & UHCI_STS_HCH) {
        kprintf("uhci: controller failed to start\n");
        return -1;
    }

    /* Register as USB HCD */
    uhci_ctrl.hcd.name = "uhci0";
    uhci_ctrl.hcd.hcd_index = 0;
    uhci_ctrl.hcd.submit = uhci_submit;
    uhci_ctrl.hcd.nports = UHCI_NUM_PORTS;
    uhci_ctrl.hcd.port_status = uhci_port_status;
    uhci_ctrl.hcd.port_reset = uhci_port_reset;
    uhci_ctrl.hcd.port_enable = uhci_port_enable;
    uhci_ctrl.hcd.priv = &uhci_ctrl;

    usb_register_hcd(&uhci_ctrl.hcd);

    uhci_initialized = 1;

    kprintf("uhci: controller initialized and running\n");
    return 0;
}

/*
 * ============================================================
 * Driver Model Integration
 * ============================================================
 */

extern struct bus_type pci_bus_type;

static const device_id_t uhci_pci_ids[] = {
    /* Match any UHCI controller: class 0x0C, subclass 0x03, progif 0x00 */
    { DEVICE_ID_ANY, DEVICE_ID_ANY, 0x000C0300U, 0x00FFFFFFU, 0 },
    { 0, 0, 0, 0, 0 }, /* sentinel */
};

static struct driver uhci_pci_driver = {
    .name     = "uhci",
    .id_table = uhci_pci_ids,
    .attach   = uhci_pci_attach,
};

void uhci_init(void)
{
    if (!pci_present())
        return;

    driver_register(&uhci_pci_driver, &pci_bus_type);
}
