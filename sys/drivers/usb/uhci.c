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
#include <kern/console.h>
#include <sys/dma.h>
#include <sys/irq.h>
#include <vm/vm_kmem.h>
#include <string.h>

/* I/O port access primitives */
static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint16_t inw(uint16_t port)
{
    uint16_t val;
    __asm__ volatile("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static inline void outl(uint16_t port, uint32_t val)
{
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint32_t inl(uint16_t port)
{
    uint32_t val;
    __asm__ volatile("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

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

    /* TD pool */
    struct uhci_td *td_pool;
    dma_addr_t      td_pool_dma;
    uint8_t         td_used[UHCI_MAX_TDS];

    /* QH pool */
    struct uhci_qh *qh_pool;
    dma_addr_t      qh_pool_dma;
    uint8_t         qh_used[UHCI_MAX_QHS];

    /* Skeleton QH for async (bulk/control) transfers */
    struct uhci_qh *async_qh;
    dma_addr_t      async_qh_dma;

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
    for (int i = 0; i < UHCI_MAX_TDS; i++) {
        if (!hc->td_used[i]) {
            hc->td_used[i] = 1;
            struct uhci_td *td = &hc->td_pool[i];
            memset(td, 0, sizeof(*td));
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

    if (port < 1 || port > UHCI_NUM_PORTS)
        return -1;

    reg = uhci_portsc_reg(port);

    /* Assert reset */
    portsc = uhci_readw(hc, reg);
    uhci_writew(hc, reg, portsc | UHCI_PORTSC_PR);

    /* Hold reset for 50ms (USB spec minimum 10ms, UHCI recommends 50ms) */
    deadline = (uint64_t)get_uptime_ms() + 50;
    while ((uint64_t)get_uptime_ms() < deadline)
        __asm__ volatile("pause");

    /* Deassert reset */
    portsc = uhci_readw(hc, reg);
    uhci_writew(hc, reg, portsc & ~UHCI_PORTSC_PR);

    /* Wait for reset to complete and port to stabilize */
    deadline = (uint64_t)get_uptime_ms() + 100;
    while ((uint64_t)get_uptime_ms() < deadline)
        __asm__ volatile("pause");

    /* Enable port */
    portsc = uhci_readw(hc, reg);
    uhci_writew(hc, reg, portsc | UHCI_PORTSC_PE);

    /* Clear status change bits (write-1-to-clear) */
    portsc = uhci_readw(hc, reg);
    uhci_writew(hc, reg, portsc | UHCI_PORTSC_CSC | UHCI_PORTSC_PEC);

    /* Verify port is enabled */
    portsc = uhci_readw(hc, reg);
    if (!(portsc & UHCI_PORTSC_PE)) {
        /* Try enabling again */
        uhci_writew(hc, reg, portsc | UHCI_PORTSC_PE);
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

    (void)hc;

    while (td) {
        /* Poll until this TD is no longer active */
        while (td->ctrl_status & UHCI_TD_CTRL_ACTIVE) {
            if ((uint64_t)get_uptime_ms() > deadline)
                return USB_XFER_TIMEOUT;
            __asm__ volatile("pause");
        }

        /* Check for errors */
        if (td->ctrl_status & UHCI_TD_CTRL_STALLED)
            return USB_XFER_STALL;
        if (td->ctrl_status & (UHCI_TD_CTRL_DBUFERR | UHCI_TD_CTRL_BABBLE |
                                UHCI_TD_CTRL_CRCTMO | UHCI_TD_CTRL_BITSTUFF))
            return USB_XFER_ERROR;

        /* Accumulate actual transfer length */
        uint32_t actlen = (td->ctrl_status & UHCI_TD_ACTLEN_MASK);
        if (actlen != UHCI_TD_ACTLEN_NULL)
            total += actlen + 1;

        /* Check for short packet (less than max expected) */
        uint32_t maxlen = ((td->token >> 21) & 0x7FF);
        if (actlen != UHCI_TD_ACTLEN_NULL && maxlen != 0x7FF &&
            actlen < maxlen) {
            /* Short packet — stop here */
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
    dma_addr_t setup_buf_dma, data_buf_dma;
    uint8_t *setup_buf;
    uint8_t addr, ep_num;
    uint8_t toggle;
    int is_in;
    uint32_t actual = 0;
    int ret;

    addr = xfer->dev->address;
    ep_num = xfer->ep->address & USB_EP_NUM_MASK;
    is_in = (xfer->setup.bmRequestType & USB_DIR_IN) ? 1 : 0;

    /* Allocate DMA buffer for setup packet (8 bytes) */
    setup_buf = dma_alloc_coherent(8, &setup_buf_dma);
    if (!setup_buf)
        return USB_XFER_ERROR;
    memcpy(setup_buf, &xfer->setup, 8);

    /* Setup TD: always DATA0 */
    setup_td = uhci_alloc_td(hc, &setup_phys);
    if (!setup_td) {
        dma_free_coherent(setup_buf, 8);
        return USB_XFER_ERROR;
    }

    setup_td->ctrl_status = UHCI_TD_CTRL_ACTIVE |
                            (3U << UHCI_TD_CTRL_CERR_SHIFT) |
                            ((xfer->dev->speed == USB_SPEED_LOW) ? UHCI_TD_CTRL_LS : 0);
    setup_td->token = UHCI_TD_TOKEN(UHCI_TD_PID_SETUP, addr, ep_num, 0, 8);
    setup_td->buffer = (uint32_t)setup_buf_dma;

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
    /* Status uses opposite direction, always DATA1 */
    if (is_in || (xfer->length == 0)) {
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

    dma_free_coherent(setup_buf, 8);
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
                          UHCI_TD_CTRL_SPD |
                          ((xfer->dev->speed == USB_SPEED_LOW) ? UHCI_TD_CTRL_LS : 0);
        td->token = UHCI_TD_TOKEN(
            is_in ? UHCI_TD_PID_IN : UHCI_TD_PID_OUT,
            addr, ep_num, xfer->ep->toggle, chunk);
        td->buffer = (uint32_t)(data_dma + offset);
        td->link = UHCI_TD_LINK_T;

        xfer->ep->toggle ^= 1;

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
 * HCD submit callback — dispatches to control or bulk handler.
 */
static int uhci_submit(usb_hcd_t *hcd, usb_transfer_t *xfer)
{
    uhci_hc_t *hc = hcd->priv;

    if (xfer->is_control)
        return uhci_control_transfer(hc, xfer);

    if (xfer->ep->type == USB_EP_TYPE_BULK)
        return uhci_bulk_transfer(hc, xfer);

    kprintf("uhci: unsupported transfer type %u\n", xfer->ep->type);
    return USB_XFER_ERROR;
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
