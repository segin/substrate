/*
 * xhci.c — xHCI (USB 3.x) host controller driver.
 *
 * Implements usb_hcd_t (submit / port_status / port_reset / port_enable) for a
 * PCI xHCI controller (class 0x0C0330, MMIO BAR0).  Synchronous polling model
 * like uhci.c/ehci.c: one transfer at a time under submit_lock.
 *
 * xHCI addresses devices with commands (Enable Slot, Address Device) against
 * per-device contexts, not the classic SET_ADDRESS control transfer the USB
 * core issues.  submit() therefore translates: the first control traffic to a
 * device (still at address 0) triggers Enable Slot + Address Device (BSR=1, so
 * the device stays in Default state for the initial descriptor reads); a
 * SET_ADDRESS request is turned into Address Device (BSR=0) and recorded in the
 * address->slot map; all later transfers route by address (or, pre-address, by
 * root port) to the slot's per-endpoint transfer ring.
 */
#include <string.h>
#include <stdio.h>
#include <sys/dma.h>
#include <sys/lock.h>
#include <vm/vm_kmem.h>
#include <kern/bus.h>
#include <kern/console.h>
#include <kern/driver.h>
#include <kern/pci.h>
#include <kern/resource.h>
#include <kern/time.h>
#include <drivers/usb/xhci.h>
#include <drivers/usb/usb.h>

#define XHCI_RING_TRBS     64      /* TRBs per ring segment */
#define XHCI_MAX_SLOTS     16
#define XHCI_BOUNCE_SIZE   (64 * 1024)
#define XHCI_CMD_TIMEOUT_MS 1000
/* Bulk streams (USB 3.0 / UAS): MaxPStreams=1 -> 4 stream contexts, IDs 0-3
 * (0 reserved).  A stream-less serial UAS uses stream ID 1. */
#define XHCI_MAXP_STREAMS  1
#define XHCI_NUM_STREAMS   4

struct xhci_ring {
    struct xhci_trb *trb;
    dma_addr_t       dma;
    uint32_t         enq;      /* enqueue index */
    uint8_t          cycle;    /* producer cycle state */
};

struct xhci_slot {
    int      in_use;
    uint8_t  port;                          /* root port (1-based) */
    uint8_t *dev_ctx;   dma_addr_t dev_ctx_dma;
    uint8_t *in_ctx;    dma_addr_t in_ctx_dma;
    struct xhci_ring ep_ring[32];           /* by DCI (1 = EP0); non-streamed */
    /* Bulk-stream endpoints (max_streams>0): a stream context array + one
     * transfer ring per stream ID, both keyed by DCI. */
    uint8_t *stream_ctx[32];  dma_addr_t stream_ctx_dma[32];
    struct xhci_ring stream_ring[32][XHCI_NUM_STREAMS];
};

typedef struct xhci_hc {
    volatile uint8_t *mmio;
    volatile uint8_t *op;
    volatile uint8_t *rt;      /* runtime */
    volatile uint8_t *db;      /* doorbell array */
    uint8_t  nports;
    uint8_t  maxslots;
    uint32_t ctx_size;         /* 32 or 64 */
    int      initialized;

    mutex_t  submit_lock;

    uint64_t *dcbaa;   dma_addr_t dcbaa_dma;
    struct xhci_ring cmd_ring;
    struct xhci_trb  *event_ring;  dma_addr_t event_ring_dma;
    uint8_t          *erst;        dma_addr_t erst_dma;
    uint32_t         event_deq;    uint8_t event_cycle;
    void             *bounce;      dma_addr_t bounce_dma;

    struct xhci_slot slots[XHCI_MAX_SLOTS + 1];   /* indexed by slot id (1-based) */
    uint8_t  addr_slot[128];       /* usb address -> slot id */
    uint8_t  enum_slot;            /* slot of the current address-0 device */
    uint8_t  enum_port;

    usb_hcd_t hcd;
} xhci_hc_t;

static xhci_hc_t xhci_ctrl;

/* ---- register access ---- */
static inline uint32_t rd32(volatile uint8_t *b, uint32_t r) { return *(volatile uint32_t *)(b + r); }
static inline void wr32(volatile uint8_t *b, uint32_t r, uint32_t v) { *(volatile uint32_t *)(b + r) = v; }
static inline void wr64(volatile uint8_t *b, uint32_t r, uint64_t v) {
    *(volatile uint32_t *)(b + r) = (uint32_t)v;
    *(volatile uint32_t *)(b + r + 4) = (uint32_t)(v >> 32);
}
static inline uint32_t portsc_rd(xhci_hc_t *hc, uint8_t p) { return rd32(hc->op, XHCI_OP_PORTSC(p - 1)); }
static inline void portsc_wr(xhci_hc_t *hc, uint8_t p, uint32_t v) { wr32(hc->op, XHCI_OP_PORTSC(p - 1), v); }

static void xhci_delay_ms(uint32_t ms)
{
    uint64_t deadline = (uint64_t)get_uptime_ms() + ms;
    while ((uint64_t)get_uptime_ms() < deadline)
        __asm__ volatile("pause");
}

/* ---- ring helpers ---- */
static int xhci_ring_alloc(struct xhci_ring *r)
{
    r->trb = dma_alloc_coherent(XHCI_RING_TRBS * sizeof(struct xhci_trb), &r->dma);
    if (!r->trb) return -1;
    memset(r->trb, 0, XHCI_RING_TRBS * sizeof(struct xhci_trb));
    /* Last TRB is a Link back to the start (toggle cycle). */
    struct xhci_trb *link = &r->trb[XHCI_RING_TRBS - 1];
    link->param = r->dma;
    link->control = XHCI_TRB_TYPE(TRB_LINK) | XHCI_TRB_TC;
    r->enq = 0;
    r->cycle = 1;
    return 0;
}

/* Push a TRB (param/status/type+flags) onto a ring; returns the phys addr of
 * the slot it landed in.  Handles the trailing Link TRB + cycle toggle. */
static uint64_t xhci_ring_push(struct xhci_ring *r, uint64_t param, uint32_t status,
                               uint32_t ctrl_type_flags)
{
    struct xhci_trb *t = &r->trb[r->enq];
    uint64_t phys = r->dma + (dma_addr_t)r->enq * sizeof(struct xhci_trb);
    t->param = param;
    t->status = status;
    /* set the type/flags, then the cycle bit last (ownership handoff) */
    t->control = ctrl_type_flags | (r->cycle ? XHCI_TRB_CYCLE : 0);

    r->enq++;
    if (r->enq == XHCI_RING_TRBS - 1) {
        /* hit the Link TRB: flip its cycle to hand it to the controller */
        struct xhci_trb *link = &r->trb[XHCI_RING_TRBS - 1];
        link->control = (link->control & ~XHCI_TRB_CYCLE) | (r->cycle ? XHCI_TRB_CYCLE : 0);
        r->enq = 0;
        r->cycle ^= 1;
    }
    return phys;
}

static void xhci_doorbell(xhci_hc_t *hc, uint32_t slot, uint32_t target)
{
    wr32(hc->db, slot * 4, target);
}

/* Wait for and consume the next event; returns the completion code, and (if
 * non-NULL) the event's param and control words.  0 = no event within timeout. */
static int xhci_wait_event(xhci_hc_t *hc, uint64_t *out_param, uint32_t *out_ctrl,
                           uint32_t *out_status)
{
    uint64_t deadline = (uint64_t)get_uptime_ms() + XHCI_CMD_TIMEOUT_MS;
    for (;;) {
        struct xhci_trb *e = &hc->event_ring[hc->event_deq];
        uint32_t ctrl = e->control;
        if ((ctrl & XHCI_TRB_CYCLE) == hc->event_cycle) {
            uint32_t cc = XHCI_TRB_GET_CC(e->status);
            if (out_param) *out_param = e->param;
            if (out_ctrl) *out_ctrl = ctrl;
            if (out_status) *out_status = e->status;
            /* advance dequeue + wrap */
            hc->event_deq++;
            if (hc->event_deq == XHCI_RING_TRBS) {
                hc->event_deq = 0;
                hc->event_cycle ^= 1;
            }
            /* update ERDP (with EHB write-back) */
            uint64_t erdp = hc->event_ring_dma +
                            (dma_addr_t)hc->event_deq * sizeof(struct xhci_trb);
            wr64(hc->rt, XHCI_RT_IR0 + XHCI_IR_ERDP, erdp | XHCI_ERDP_EHB);
            return (int)cc;
        }
        if ((uint64_t)get_uptime_ms() > deadline)
            return 0;
        __asm__ volatile("pause");
    }
}

/* Run a command TRB on the command ring; return (completion code, slot id). */
static int xhci_run_command(xhci_hc_t *hc, uint64_t param, uint32_t ctrl,
                            uint8_t *out_slot)
{
    xhci_ring_push(&hc->cmd_ring, param, 0, ctrl);
    xhci_doorbell(hc, 0, 0);   /* ring the command doorbell */
    uint64_t ep; uint32_t ec;
    for (int guard = 0; guard < 8; guard++) {
        int cc = xhci_wait_event(hc, &ep, &ec, NULL);
        if (cc == 0) return 0;   /* timeout */
        if (XHCI_TRB_GET_TYPE(ec) == TRB_CMD_COMPLETE) {
            if (out_slot) *out_slot = XHCI_TRB_GET_SLOT(ec);
            return cc;
        }
        /* skip unrelated events (e.g. port status) and keep looking */
    }
    return 0;
}

/* [DRV-03] Wait for the Transfer Event addressed to (slot, dci); skip unrelated
 * events (port-status changes from the hotplug scanner, other slots/endpoints).
 * Without this filter a routine Port-Status-Change event is consumed as the
 * transfer's completion, giving a bogus actual_length and desyncing every later
 * waiter.  Mirrors xhci_run_command's TRB-type filtering.  Returns the completion
 * code (0 on timeout); on a match *out_status gets the event's status dword.
 * Transfer Event control dword: [31:24] slot id, [20:16] endpoint id (DCI). */
static int xhci_wait_transfer(xhci_hc_t *hc, uint8_t slot, int dci,
                              uint32_t *out_status)
{
    uint32_t ec, est;
    for (int guard = 0; guard < 8; guard++) {
        int cc = xhci_wait_event(hc, NULL, &ec, &est);
        if (cc == 0) return 0;   /* timeout */
        if (XHCI_TRB_GET_TYPE(ec) == TRB_TRANSFER_EVENT &&
            XHCI_TRB_GET_SLOT(ec) == slot &&
            (int)((ec >> 16) & 0x1F) == dci) {
            if (out_status) *out_status = est;
            return cc;
        }
        /* non-matching event (port status, other slot/endpoint): drop, keep looking */
    }
    return 0;
}

/* ---- context helpers ---- */
static uint8_t *slot_ctx_of(xhci_hc_t *hc, uint8_t *dev_ctx) { (void)hc; return dev_ctx; }
/* input context: [0]=input-control, [1]=slot, [1+dci]=ep */
static uint8_t *in_ctrl_of(uint8_t *in_ctx) { return in_ctx; }
static uint8_t *in_slot_of(xhci_hc_t *hc, uint8_t *in_ctx) { return in_ctx + hc->ctx_size; }
static uint8_t *in_ep_of(xhci_hc_t *hc, uint8_t *in_ctx, int dci)
{
    return in_ctx + (uint32_t)(dci + 1) * hc->ctx_size;
}

/* ---- port ops ---- */
static uint32_t xhci_port_status(usb_hcd_t *hcd, uint8_t port)
{
    xhci_hc_t *hc = hcd->priv;
    uint32_t psc = portsc_rd(hc, port);
    uint32_t out = 0;
    if (psc & XHCI_PORT_CCS) out |= USB_PORT_STAT_CONNECTION;
    if (psc & XHCI_PORT_PED) out |= USB_PORT_STAT_ENABLE;
    /* xHCI usb-storage is SuperSpeed/High: report high-speed for the core. */
    if (psc & XHCI_PORT_CCS) out |= USB_PORT_STAT_HIGH_SPEED;
    return out;
}

static int xhci_port_reset(usb_hcd_t *hcd, uint8_t port)
{
    xhci_hc_t *hc = hcd->priv;
    uint32_t psc = portsc_rd(hc, port);
    if (!(psc & XHCI_PORT_CCS)) return -1;

    /* USB3 ports often auto-enable on connect; if already enabled, done. */
    if (psc & XHCI_PORT_PED)
        return 0;

    psc = portsc_rd(hc, port) & ~XHCI_PORT_CHANGE_MASK;
    portsc_wr(hc, port, psc | XHCI_PORT_PR);
    for (int i = 0; i < 100; i++) {
        xhci_delay_ms(2);
        psc = portsc_rd(hc, port);
        if (psc & XHCI_PORT_PRC) {           /* reset complete */
            portsc_wr(hc, port, (psc & ~XHCI_PORT_CHANGE_MASK) | XHCI_PORT_PRC);
            break;
        }
    }
    psc = portsc_rd(hc, port);
    return (psc & XHCI_PORT_PED) ? 0 : -1;
}

static int xhci_port_enable(usb_hcd_t *hcd, uint8_t port, int enable)
{
    (void)hcd; (void)port; (void)enable;
    return 0;   /* xHCI enables ports via reset; nothing to do */
}

/* ---- slot setup: Enable Slot + Address Device ---- */
static int xhci_setup_slot(xhci_hc_t *hc, usb_transfer_t *xfer, uint8_t port)
{
    uint8_t slot = 0;
    int cc = xhci_run_command(hc, 0, XHCI_TRB_TYPE(TRB_ENABLE_SLOT), &slot);
    if (cc != XHCI_CC_SUCCESS || slot == 0 || slot > XHCI_MAX_SLOTS) {
        kprintf("xhci: enable slot failed (cc=%d slot=%u)\n", cc, slot);
        return -1;
    }
    struct xhci_slot *s = &hc->slots[slot];
    memset(s, 0, sizeof(*s));
    s->in_use = 1;
    s->port = port;

    s->dev_ctx = dma_alloc_coherent(32 * hc->ctx_size, &s->dev_ctx_dma);
    s->in_ctx  = dma_alloc_coherent(33 * hc->ctx_size, &s->in_ctx_dma);
    if (!s->dev_ctx || !s->in_ctx || xhci_ring_alloc(&s->ep_ring[1]) != 0) {
        kprintf("xhci: slot %u alloc failed\n", slot);
        return -1;
    }
    memset(s->dev_ctx, 0, 32 * hc->ctx_size);
    memset(s->in_ctx, 0, 33 * hc->ctx_size);
    hc->dcbaa[slot] = s->dev_ctx_dma;

    /* Input control: add slot ctx (A0) + EP0 ctx (A1). */
    uint32_t *icc = (uint32_t *)in_ctrl_of(s->in_ctx);
    icc[1] = 0x3;   /* add flags: bit0 slot, bit1 EP0 */

    /* Slot context: 1 ctx entry (EP0), speed, root hub port. */
    uint32_t *sc = (uint32_t *)in_slot_of(hc, s->in_ctx);
    uint32_t speed = (portsc_rd(hc, port) & XHCI_PORT_SPEED_MASK) >> XHCI_PORT_SPEED_SHIFT;
    sc[0] = (1u << XHCI_SLOT_CTX_ENTRIES_SHIFT) | (speed << XHCI_SLOT_SPEED_SHIFT);
    sc[1] = (uint32_t)port << XHCI_SLOT_RHPORT_SHIFT;

    /* EP0 context: control endpoint, max packet, transfer ring deq ptr. */
    uint32_t mps = xfer->ep ? xfer->ep->max_packet : 64;
    if (mps == 0) mps = 64;
    uint32_t *ep0 = (uint32_t *)in_ep_of(hc, s->in_ctx, 1);
    ep0[1] = (EP_TYPE_CONTROL << XHCI_EP_TYPE_SHIFT) | (mps << XHCI_EP_MPS_SHIFT) |
             (3u << XHCI_EP_CERR_SHIFT);
    ep0[2] = (uint32_t)(s->ep_ring[1].dma) | s->ep_ring[1].cycle;  /* DCS */
    ep0[3] = (uint32_t)((uint64_t)s->ep_ring[1].dma >> 32);

    /* Address Device with BSR=1: sets up the slot but leaves the device in
     * Default state so the core can read the initial 8-byte descriptor. */
    uint32_t ctrl = XHCI_TRB_TYPE(TRB_ADDRESS_DEVICE) | ((uint32_t)slot << 24) |
                    0x200 /* BSR (block set address) */;
    cc = xhci_run_command(hc, s->in_ctx_dma, ctrl, NULL);
    if (cc != XHCI_CC_SUCCESS) {
        kprintf("xhci: address-device(BSR) slot %u failed cc=%d\n", slot, cc);
        return -1;
    }
    hc->enum_slot = slot;
    hc->enum_port = port;
    return slot;
}

/* Resolve the slot backing a transfer (by address, else the enumerating slot). */
static uint8_t xhci_slot_for(xhci_hc_t *hc, usb_transfer_t *xfer)
{
    uint8_t addr = xfer->dev->address & 0x7F;
    if (addr != 0 && hc->addr_slot[addr])
        return hc->addr_slot[addr];
    if (hc->enum_slot && hc->slots[hc->enum_slot].port == xfer->dev->port)
        return hc->enum_slot;
    return 0;
}

/* Ensure a bulk/interrupt endpoint's transfer ring + context exist (lazy
 * Configure Endpoint), then return its DCI. */
static int xhci_ensure_ep(xhci_hc_t *hc, uint8_t slot, usb_transfer_t *xfer)
{
    uint8_t epaddr = xfer->ep->address;
    uint8_t epnum = epaddr & 0x0F;
    int in = (epaddr & 0x80) != 0;
    int dci = epnum * 2 + (in ? 1 : 0);
    struct xhci_slot *s = &hc->slots[slot];
    int streamed = (xfer->ep->max_streams > 0);

    if (streamed ? (s->stream_ctx[dci] != NULL) : (s->ep_ring[dci].trb != NULL))
        return dci;   /* already configured */

    if (streamed) {
        /* Stream Context Array: XHCI_NUM_STREAMS entries of 16 bytes.  Per-stream
         * transfer rings are allocated lazily in xhci_bulk on first use. */
        s->stream_ctx[dci] = dma_alloc_coherent(XHCI_NUM_STREAMS * 16,
                                                &s->stream_ctx_dma[dci]);
        if (!s->stream_ctx[dci])
            return -1;
        memset(s->stream_ctx[dci], 0, XHCI_NUM_STREAMS * 16);
    } else if (xhci_ring_alloc(&s->ep_ring[dci]) != 0) {
        return -1;
    }

    /* Input context: add flags = slot (A0, must re-supply) + this EP. */
    memset(s->in_ctx, 0, 33 * hc->ctx_size);
    uint32_t *icc = (uint32_t *)in_ctrl_of(s->in_ctx);
    icc[1] = 0x1 | (1u << dci);
    /* slot ctx: bump context-entries to include this DCI */
    uint32_t *insc = (uint32_t *)in_slot_of(hc, s->in_ctx);
    uint32_t *dsc = (uint32_t *)slot_ctx_of(hc, s->dev_ctx);
    insc[0] = (dsc[0] & ~(0x1Fu << XHCI_SLOT_CTX_ENTRIES_SHIFT)) |
              ((uint32_t)dci << XHCI_SLOT_CTX_ENTRIES_SHIFT);
    insc[1] = dsc[1];

    uint32_t type = xfer->ep->type == USB_EP_TYPE_BULK
                        ? (in ? EP_TYPE_BULK_IN : EP_TYPE_BULK_OUT)
                        : (in ? EP_TYPE_INT_IN : EP_TYPE_INT_OUT);
    uint32_t mps = xfer->ep->max_packet ? xfer->ep->max_packet : 512;
    uint32_t *epc = (uint32_t *)in_ep_of(hc, s->in_ctx, dci);
    epc[1] = (type << XHCI_EP_TYPE_SHIFT) | (mps << XHCI_EP_MPS_SHIFT) |
             (3u << XHCI_EP_CERR_SHIFT);
    if (streamed) {
        /* Streamed EP: MaxPStreams + Linear Stream Array; the TR-dequeue field
         * holds the Stream Context Array base (no DCS -- that is per-stream). */
        epc[0] = (XHCI_MAXP_STREAMS << XHCI_EP_MAXPSTREAMS_SHIFT) | XHCI_EP_LSA;
        epc[2] = (uint32_t)s->stream_ctx_dma[dci];
        epc[3] = (uint32_t)((uint64_t)s->stream_ctx_dma[dci] >> 32);
    } else {
        epc[2] = (uint32_t)s->ep_ring[dci].dma | s->ep_ring[dci].cycle;
        epc[3] = (uint32_t)((uint64_t)s->ep_ring[dci].dma >> 32);
    }

    uint32_t ctrl = XHCI_TRB_TYPE(TRB_CONFIGURE_EP) | ((uint32_t)slot << 24);
    int cc = xhci_run_command(hc, s->in_ctx_dma, ctrl, NULL);
    if (cc != XHCI_CC_SUCCESS) {
        kprintf("xhci: configure-ep slot %u dci %d (streamed=%d) failed cc=%d\n",
                slot, dci, streamed, cc);
        return -1;
    }
    return dci;
}

/* ---- transfers ---- */
static int xhci_control(xhci_hc_t *hc, usb_transfer_t *xfer)
{
    uint8_t port = xfer->dev->port;
    uint8_t addr = xfer->dev->address & 0x7F;

    uint8_t slot = xhci_slot_for(hc, xfer);
    if (slot == 0) {
        int r = xhci_setup_slot(hc, xfer, port);
        if (r < 0) return USB_XFER_ERROR;
        slot = (uint8_t)r;
    }

    /* Intercept SET_ADDRESS: satisfy it with Address Device (BSR=0). */
    if ((xfer->setup.bmRequestType == 0x00) &&
        xfer->setup.bRequest == USB_REQ_SET_ADDRESS) {
        struct xhci_slot *s = &hc->slots[slot];
        uint32_t ctrl = XHCI_TRB_TYPE(TRB_ADDRESS_DEVICE) | ((uint32_t)slot << 24);
        int cc = xhci_run_command(hc, s->in_ctx_dma, ctrl, NULL);
        if (cc != XHCI_CC_SUCCESS) return USB_XFER_ERROR;
        hc->addr_slot[xfer->setup.wValue & 0x7F] = slot;
        hc->enum_slot = 0;
        xfer->actual_length = 0;
        xfer->status = USB_XFER_OK;
        return USB_XFER_OK;
    }

    struct xhci_ring *ring = &hc->slots[slot].ep_ring[1];
    uint32_t len = xfer->length;
    int in = (xfer->setup.bmRequestType & 0x80) != 0;
    if (len > XHCI_BOUNCE_SIZE) return USB_XFER_ERROR;
    if (!in && len) memcpy(hc->bounce, xfer->data, len);

    /* Setup stage: immediate 8-byte setup data. */
    uint64_t setup_param;
    memcpy(&setup_param, &xfer->setup, 8);
    uint32_t trt = len ? (in ? TRB_TRT_IN : TRB_TRT_OUT) : TRB_TRT_NO_DATA;
    xhci_ring_push(ring, setup_param, 8,
                   XHCI_TRB_TYPE(TRB_SETUP) | XHCI_TRB_IDT | trt);
    if (len) {
        xhci_ring_push(ring, hc->bounce_dma, len,
                       XHCI_TRB_TYPE(TRB_DATA) | (in ? TRB_DIR_IN : 0));
    }
    /* Status stage: opposite direction, IOC so we get a Transfer Event. */
    uint32_t status_dir = (in && len) ? 0 : TRB_DIR_IN;
    xhci_ring_push(ring, 0, 0,
                   XHCI_TRB_TYPE(TRB_STATUS) | status_dir | XHCI_TRB_IOC);

    xhci_doorbell(hc, slot, 1);   /* DCI 1 = EP0 */

    uint32_t evst;
    int cc = xhci_wait_transfer(hc, slot, 1, &evst);   /* [DRV-03] EP0 = DCI 1 */
    if (cc != XHCI_CC_SUCCESS && cc != XHCI_CC_SHORT_PACKET)
        return (cc == 0) ? USB_XFER_TIMEOUT : USB_XFER_STALL;

    uint32_t residue = XHCI_TRB_GET_XLEN(evst);
    xfer->actual_length = (len > residue) ? (len - residue) : len;
    if (in && len) memcpy(xfer->data, hc->bounce, xfer->actual_length);
    xfer->status = USB_XFER_OK;
    (void)addr;
    return USB_XFER_OK;
}

static int xhci_bulk(xhci_hc_t *hc, usb_transfer_t *xfer)
{
    uint8_t slot = xhci_slot_for(hc, xfer);
    if (slot == 0) return USB_XFER_ERROR;
    int dci = xhci_ensure_ep(hc, slot, xfer);
    if (dci < 0) return USB_XFER_ERROR;
    struct xhci_slot *s = &hc->slots[slot];

    struct xhci_ring *ring;
    uint32_t db_target;
    if (xfer->ep->max_streams > 0) {
        /* Streamed endpoint: pick the stream's transfer ring (allocating it and
         * its stream-context-array entry on first use) and ring the doorbell
         * with the stream ID. */
        uint16_t sid = xfer->stream_id ? xfer->stream_id : 1;
        if (sid >= XHCI_NUM_STREAMS) sid = 1;
        ring = &s->stream_ring[dci][sid];
        if (!ring->trb) {
            if (xhci_ring_alloc(ring) != 0) return USB_XFER_ERROR;
            uint8_t *sc = s->stream_ctx[dci] + (uint32_t)sid * 16;
            *(volatile uint64_t *)sc =
                (uint64_t)ring->dma | XHCI_SCTX_SCT_PRIM_TR | ring->cycle;
        }
        db_target = (uint32_t)dci | ((uint32_t)sid << XHCI_DB_STREAM_SHIFT);
    } else {
        ring = &s->ep_ring[dci];
        db_target = (uint32_t)dci;
    }

    uint32_t len = xfer->length;
    int in = (xfer->ep->address & 0x80) != 0;
    if (len > XHCI_BOUNCE_SIZE) return USB_XFER_ERROR;
    if (!in && len) memcpy(hc->bounce, xfer->data, len);

    xhci_ring_push(ring, len ? hc->bounce_dma : 0, len,
                   XHCI_TRB_TYPE(TRB_NORMAL) | XHCI_TRB_IOC | XHCI_TRB_ISP);
    xhci_doorbell(hc, slot, db_target);

    uint32_t evst;
    int cc = xhci_wait_transfer(hc, slot, dci, &evst);   /* [DRV-03] this EP's DCI */
    if (cc != XHCI_CC_SUCCESS && cc != XHCI_CC_SHORT_PACKET)
        return (cc == 0) ? USB_XFER_TIMEOUT : USB_XFER_STALL;

    uint32_t residue = XHCI_TRB_GET_XLEN(evst);
    xfer->actual_length = (len > residue) ? (len - residue) : len;
    if (in && xfer->actual_length) memcpy(xfer->data, hc->bounce, xfer->actual_length);
    xfer->status = USB_XFER_OK;
    return USB_XFER_OK;
}

static int xhci_submit(usb_hcd_t *hcd, usb_transfer_t *xfer)
{
    xhci_hc_t *hc = hcd->priv;
    int ret;
    mutex_lock(&hc->submit_lock);
    if (xfer->is_control)
        ret = xhci_control(hc, xfer);
    else if (xfer->ep && (xfer->ep->type == USB_EP_TYPE_BULK ||
                          xfer->ep->type == USB_EP_TYPE_INTERRUPT))
        ret = xhci_bulk(hc, xfer);
    else
        ret = USB_XFER_ERROR;
    mutex_unlock(&hc->submit_lock);
    return ret;
}

/* ---- init ---- */
static int xhci_reset(xhci_hc_t *hc)
{
    /* wait CNR clear */
    for (int i = 0; i < 100 && (rd32(hc->op, XHCI_OP_USBSTS) & XHCI_STS_CNR); i++)
        xhci_delay_ms(1);
    wr32(hc->op, XHCI_OP_USBCMD, rd32(hc->op, XHCI_OP_USBCMD) & ~XHCI_CMD_RUN);
    for (int i = 0; i < 100 && !(rd32(hc->op, XHCI_OP_USBSTS) & XHCI_STS_HCH); i++)
        xhci_delay_ms(1);
    wr32(hc->op, XHCI_OP_USBCMD, XHCI_CMD_HCRST);
    for (int i = 0; i < 500; i++) {
        if (!(rd32(hc->op, XHCI_OP_USBCMD) & XHCI_CMD_HCRST) &&
            !(rd32(hc->op, XHCI_OP_USBSTS) & XHCI_STS_CNR))
            return 0;
        xhci_delay_ms(1);
    }
    return -1;
}

static int xhci_start(xhci_hc_t *hc)
{
    if (xhci_reset(hc) != 0) { kprintf("xhci: reset timeout\n"); return -1; }

    wr32(hc->op, XHCI_OP_CONFIG, hc->maxslots);

    hc->dcbaa = dma_alloc_coherent((XHCI_MAX_SLOTS + 1) * 8, &hc->dcbaa_dma);
    if (!hc->dcbaa) return -1;
    memset(hc->dcbaa, 0, (XHCI_MAX_SLOTS + 1) * 8);
    wr64(hc->op, XHCI_OP_DCBAAP, hc->dcbaa_dma);

    if (xhci_ring_alloc(&hc->cmd_ring) != 0) return -1;
    wr64(hc->op, XHCI_OP_CRCR, hc->cmd_ring.dma | XHCI_CRCR_RCS);

    /* Event ring: one segment + a one-entry ERST. */
    hc->event_ring = dma_alloc_coherent(XHCI_RING_TRBS * sizeof(struct xhci_trb),
                                        &hc->event_ring_dma);
    hc->erst = dma_alloc_coherent(64, &hc->erst_dma);
    if (!hc->event_ring || !hc->erst) return -1;
    memset(hc->event_ring, 0, XHCI_RING_TRBS * sizeof(struct xhci_trb));
    memset(hc->erst, 0, 64);
    *(uint64_t *)(hc->erst + 0) = hc->event_ring_dma;
    *(uint32_t *)(hc->erst + 8) = XHCI_RING_TRBS;
    hc->event_deq = 0;
    hc->event_cycle = 1;

    wr32(hc->rt, XHCI_RT_IR0 + XHCI_IR_ERSTSZ, 1);
    wr64(hc->rt, XHCI_RT_IR0 + XHCI_IR_ERDP, hc->event_ring_dma);
    wr64(hc->rt, XHCI_RT_IR0 + XHCI_IR_ERSTBA, hc->erst_dma);
    wr32(hc->rt, XHCI_RT_IR0 + XHCI_IR_IMAN, 0);   /* polling: no interrupts */

    hc->bounce = dma_alloc_coherent(XHCI_BOUNCE_SIZE, &hc->bounce_dma);
    if (!hc->bounce) return -1;

    wr32(hc->op, XHCI_OP_USBCMD, XHCI_CMD_RUN);
    for (int i = 0; i < 100 && (rd32(hc->op, XHCI_OP_USBSTS) & XHCI_STS_HCH); i++)
        xhci_delay_ms(1);

    /* Power all ports. */
    for (uint8_t p = 1; p <= hc->nports; p++) {
        uint32_t psc = portsc_rd(hc, p) & ~XHCI_PORT_CHANGE_MASK;
        portsc_wr(hc, p, psc | XHCI_PORT_PP);
    }
    xhci_delay_ms(20);
    return 0;
}

static int xhci_pci_attach(struct device *dev)
{
    if (xhci_ctrl.initialized) return -1;
    pci_device_t *pdev = pci_find_device_by_kdev(dev);
    if (!pdev) return -1;

    int bt = pci_bar_type(pdev, 0);
    if (bt != PCI_BAR_MEM32 && bt != PCI_BAR_MEM64) return -1;
    uint32_t bar0 = pci_read_config32(pdev->bus, pdev->slot, pdev->func, 0x10);
    uintptr_t phys = bar0 & ~0xFUL;

    uint16_t cmd = pci_read_config16(pdev->bus, pdev->slot, pdev->func, PCI_CONFIG_COMMAND);
    pci_write_config16(pdev->bus, pdev->slot, pdev->func, PCI_CONFIG_COMMAND,
                       cmd | 0x0002 | 0x0004);

    xhci_hc_t *hc = &xhci_ctrl;
    hc->mmio = ioremap(phys, 0x4000);   /* cover cap+op(0)+runtime(0x1000)+doorbell(0x2000) */
    if (!hc->mmio) { kprintf("xhci: ioremap failed\n"); return -1; }
    uint8_t caplen = *(volatile uint8_t *)(hc->mmio + XHCI_CAP_CAPLENGTH);
    hc->op = hc->mmio + caplen;
    hc->rt = hc->mmio + (rd32(hc->mmio, XHCI_CAP_RTSOFF) & ~0x1Fu);
    hc->db = hc->mmio + (rd32(hc->mmio, XHCI_CAP_DBOFF) & ~0x3u);
    uint32_t hcs1 = rd32(hc->mmio, XHCI_CAP_HCSPARAMS1);
    hc->nports = XHCI_HCS1_MAXPORTS(hcs1);
    hc->maxslots = XHCI_HCS1_MAXSLOTS(hcs1);
    if (hc->maxslots > XHCI_MAX_SLOTS) hc->maxslots = XHCI_MAX_SLOTS;
    hc->ctx_size = (rd32(hc->mmio, XHCI_CAP_HCCPARAMS1) & XHCI_HCC1_CSZ) ? 64 : 32;
    if (hc->nports == 0) hc->nports = 1;

    mutex_init(&hc->submit_lock, "xhci_submit");
    if (xhci_start(hc) != 0) return -1;

    hc->hcd.priv = hc;
    hc->hcd.name = "xhci0";
    hc->hcd.nports = hc->nports;
    hc->hcd.submit = xhci_submit;
    hc->hcd.port_status = xhci_port_status;
    hc->hcd.port_reset = xhci_port_reset;
    hc->hcd.port_enable = xhci_port_enable;
    usb_register_hcd(&hc->hcd);
    hc->initialized = 1;
    kprintf("xhci: USB 3.x controller at 0x%x, %u ports, %u slots, ctx=%u\n",
            (unsigned)phys, hc->nports, hc->maxslots, hc->ctx_size);
    return 0;
}

static const device_id_t xhci_pci_ids[] = {
    { DEVICE_ID_ANY, DEVICE_ID_ANY, 0x000C0330U, 0x00FFFFFFU, 0 },
    { 0, 0, 0, 0, 0 },
};
static struct driver xhci_pci_driver = {
    .name = "xhci", .id_table = xhci_pci_ids, .attach = xhci_pci_attach,
};

void xhci_init(void)
{
    if (!pci_present()) return;
    driver_register(&xhci_pci_driver, &pci_bus_type);
}
