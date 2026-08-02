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
#include <stdio.h>
#include <string.h>

#include <drivers/usb/usb.h>
#include <drivers/usb/xhci.h>
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

#define XHCI_MMIO_SIZE     0x4000  /* cap + op(0) + runtime(0x1000) + doorbell(0x2000) */
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

    char      name[8];  /* "xhciN", backs hcd.name */
    usb_hcd_t hcd;
} xhci_hc_t;

/*
 * One instance per PCI function.  Machines with more than one xHCI controller
 * (common once front-panel USB 3 headers sit behind a second controller) used
 * to get only the first one; the rest of the ports were dead.
 */
static uint8_t xhci_instances;

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
                           uint32_t *out_status, uint32_t timeout_ms)
{
    uint64_t deadline = (uint64_t)get_uptime_ms() + timeout_ms;
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
        int cc = xhci_wait_event(hc, &ep, &ec, NULL, XHCI_CMD_TIMEOUT_MS);
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
                              uint32_t *out_status, uint32_t timeout_ms)
{
    uint32_t ec, est;
    for (int guard = 0; guard < 8; guard++) {
        int cc = xhci_wait_event(hc, NULL, &ec, &est, timeout_ms);
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

static void xhci_free_slot(xhci_hc_t *hc, uint8_t slot);

/* [A34] Release any slot still bound to a root port that has lost its
 * connection.  The USB core has no HCD-level disconnect callback — it only runs
 * the class driver's .detach — so a successfully-enumerated device's slot was
 * never disabled and its contexts + transfer rings leaked on unplug, exhausting
 * the controller's 16 slots after a handful of cycles.  We piggy-back on the
 * hot-plug scanner's port_status poll: when a port that owns a slot reads
 * CCS=0, disable the slot and free everything, and drop the address->slot and
 * enumeration bookkeeping that pointed at it (so a re-plugged device can't route
 * through a stale slot).  submit_lock is taken because xhci_free_slot drives the
 * command/event rings, exactly like a submit. */
static void xhci_port_disconnect(xhci_hc_t *hc, uint8_t port)
{
    int found = 0;

    /* Cheap lock-free pre-check: the common case is an empty port with no slot,
     * polled several times a second — don't take submit_lock for it. */
    for (uint8_t slot = 1; slot <= XHCI_MAX_SLOTS; slot++) {
        if (hc->slots[slot].in_use && hc->slots[slot].port == port) {
            found = 1;
            break;
        }
    }
    if (!found)
        return;

    mutex_lock(&hc->submit_lock);
    for (uint8_t slot = 1; slot <= XHCI_MAX_SLOTS; slot++) {
        struct xhci_slot *s = &hc->slots[slot];
        if (!s->in_use || s->port != port)
            continue;
        for (int a = 0; a < 128; a++)
            if (hc->addr_slot[a] == slot)
                hc->addr_slot[a] = 0;
        if (hc->enum_slot == slot) {
            hc->enum_slot = 0;
            hc->enum_port = 0;
        }
        xhci_free_slot(hc, slot);
    }
    mutex_unlock(&hc->submit_lock);
}

/* ---- port ops ---- */
static uint32_t xhci_port_status(usb_hcd_t *hcd, uint8_t port)
{
    xhci_hc_t *hc = hcd->priv;
    uint32_t psc = portsc_rd(hc, port);
    uint32_t out = 0;
    if (psc & XHCI_PORT_CCS) out |= USB_PORT_STAT_CONNECTION;
    if (psc & XHCI_PORT_PED) out |= USB_PORT_STAT_ENABLE;
    if (psc & XHCI_PORT_CCS) {
        /*
         * Report the port's actual speed.  This used to claim high-speed for
         * everything, so a full-speed device got a 64-byte EP0 it never agreed
         * to and a SuperSpeed one was never recognised as such -- which also
         * meant bMaxPacketSize0's exponent encoding was read as a literal.
         * PORTSC bits 13:10 hold the Protocol Speed ID; 1..4 are the standard
         * FS/LS/HS/SS assignments (xHCI 1.1 s7.2.2.1.1). [USB-12]
         */
        uint32_t psid = (psc & XHCI_PORT_SPEED_MASK) >> XHCI_PORT_SPEED_SHIFT;
        switch (psid) {
        case 1: break;                                      /* full speed */
        case 2: out |= USB_PORT_STAT_LOW_SPEED;   break;
        case 3: out |= USB_PORT_STAT_HIGH_SPEED;  break;
        case 4: out |= USB_PORT_STAT_SUPER_SPEED; break;
        default: out |= USB_PORT_STAT_HIGH_SPEED; break;    /* unknown: assume HS */
        }
    } else {
        xhci_port_disconnect(hc, port);   /* [A34] reap the departed slot */
    }
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

/* [DRV-19] Release everything a (partially) set-up slot allocated and hand the
 * slot id back to the controller.  Every failure path in xhci_setup_slot funnels
 * here so a flaky enumeration doesn't leak the slot + its DMA contexts — 16 such
 * failures would otherwise exhaust every slot the controller has. */
static void xhci_free_slot(xhci_hc_t *hc, uint8_t slot)
{
    struct xhci_slot *s = &hc->slots[slot];

    /* Tell the controller to release the slot before we free its contexts. */
    xhci_run_command(hc, 0,
                     XHCI_TRB_TYPE(TRB_DISABLE_SLOT) | ((uint32_t)slot << 24),
                     NULL);
    hc->dcbaa[slot] = 0;

    /* Free every per-DCI resource, not just EP0's ring: a fully-enumerated
     * device also has bulk/interrupt transfer rings (xhci_ensure_ep) and, for
     * USB3 UAS, a stream-context array plus one ring per stream ID (xhci_bulk).
     * On a setup-time failure only ep_ring[1] exists, so the extra checks are
     * cheap no-ops; on a real disconnect they are what actually plugs the leak.
     * [A34] */
    for (int dci = 0; dci < 32; dci++) {
        if (s->ep_ring[dci].trb) {
            dma_free_coherent(s->ep_ring[dci].trb,
                              XHCI_RING_TRBS * sizeof(struct xhci_trb));
            s->ep_ring[dci].trb = NULL;
        }
        for (int sid = 0; sid < XHCI_NUM_STREAMS; sid++) {
            if (s->stream_ring[dci][sid].trb) {
                dma_free_coherent(s->stream_ring[dci][sid].trb,
                                  XHCI_RING_TRBS * sizeof(struct xhci_trb));
                s->stream_ring[dci][sid].trb = NULL;
            }
        }
        if (s->stream_ctx[dci]) {
            dma_free_coherent(s->stream_ctx[dci], XHCI_NUM_STREAMS * 16);
            s->stream_ctx[dci] = NULL;
        }
    }
    if (s->in_ctx) {
        dma_free_coherent(s->in_ctx, 33 * hc->ctx_size);
        s->in_ctx = NULL;
    }
    if (s->dev_ctx) {
        dma_free_coherent(s->dev_ctx, 32 * hc->ctx_size);
        s->dev_ctx = NULL;
    }
    memset(s, 0, sizeof(*s));
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
        xhci_free_slot(hc, slot);   /* [DRV-19] */
        return -1;
    }
    memset(s->dev_ctx, 0, 32 * hc->ctx_size);
    memset(s->in_ctx, 0, 33 * hc->ctx_size);
    hc->dcbaa[slot] = s->dev_ctx_dma;

    /* Input control: add slot ctx (A0) + EP0 ctx (A1). */
    uint32_t *icc = (uint32_t *)in_ctrl_of(s->in_ctx);
    icc[1] = 0x3;   /* add flags: bit0 slot, bit1 EP0 */

    /*
     * Slot context: 1 ctx entry (EP0), speed, root hub port -- plus the
     * topology fields, without which the controller cannot reach anything
     * behind a hub.  `port` here is the ROOT port; the device's own port
     * number is only one tier of the route. [USB-01]
     */
    uint32_t *sc = (uint32_t *)in_slot_of(hc, s->in_ctx);
    uint32_t speed = (portsc_rd(hc, port) & XHCI_PORT_SPEED_MASK) >> XHCI_PORT_SPEED_SHIFT;
    usb_device_t *udev = xfer->dev;
    uint32_t route = usb_route_string(udev);

    sc[0] = (1u << XHCI_SLOT_CTX_ENTRIES_SHIFT) | (speed << XHCI_SLOT_SPEED_SHIFT) |
            (route & XHCI_SLOT_ROUTE_MASK);
    sc[1] = (uint32_t)port << XHCI_SLOT_RHPORT_SHIFT;
    sc[2] = 0;

    /*
     * A low/full-speed device behind a high-speed hub is reached through that
     * hub's transaction translator; the controller needs its slot id and the
     * port the device's branch occupies on it.  Both read 0 for a device that
     * is high-speed itself or sits on a root port, which is what the spec
     * requires there.
     */
    {
        uint8_t ttport = 0;
        usb_device_t *tthub = usb_tt_hub(udev, &ttport);

        if (tthub && tthub->address && hc->addr_slot[tthub->address & 0x7F]) {
            uint8_t ttslot = hc->addr_slot[tthub->address & 0x7F];
            sc[2] = ((uint32_t)ttslot << XHCI_SLOT_TT_HUB_SHIFT) |
                    ((uint32_t)ttport << XHCI_SLOT_TT_PORT_SHIFT);
        }
    }

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
        xhci_free_slot(hc, slot);   /* [DRV-19] */
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
    if (hc->enum_slot &&
        hc->slots[hc->enum_slot].port == usb_root_port(xfer->dev))
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

/*
 * The core has learned this device is a hub.  Re-issue its slot context with
 * the Hub bit and downstream port count set: xHCI will not route a transfer
 * past a slot that does not declare itself one.  Evaluate Context is the
 * command for amending an already-addressed slot (xHCI 1.1 s4.6.7). [USB-01]
 */
static int xhci_set_hub(usb_hcd_t *hcd, usb_device_t *dev, uint8_t nports)
{
    xhci_hc_t *hc = hcd->priv;
    uint8_t slot;
    uint32_t *icc, *sc;
    int cc;

    if (!dev->address)
        return -1;
    slot = hc->addr_slot[dev->address & 0x7F];
    if (slot == 0 || slot > XHCI_MAX_SLOTS || !hc->slots[slot].in_ctx)
        return -1;

    mutex_lock(&hc->submit_lock);

    /* Evaluate Context looks at the Add flags: slot context only (A0). */
    icc = (uint32_t *)in_ctrl_of(hc->slots[slot].in_ctx);
    icc[0] = 0;
    icc[1] = 0x1;

    sc = (uint32_t *)in_slot_of(hc, hc->slots[slot].in_ctx);
    sc[0] |= XHCI_SLOT_HUB;
    sc[1] = (sc[1] & 0x00FFFFFFu) | ((uint32_t)nports << XHCI_SLOT_NPORTS_SHIFT);

    cc = xhci_run_command(hc, hc->slots[slot].in_ctx_dma,
                          XHCI_TRB_TYPE(TRB_EVAL_CONTEXT) |
                          ((uint32_t)slot << 24), NULL);

    /* Restore the add flags the transfer path expects (slot + EP0). */
    icc[1] = 0x3;
    mutex_unlock(&hc->submit_lock);

    if (cc != XHCI_CC_SUCCESS) {
        kprintf("xhci: marking slot %u as a hub failed (cc=%d)\n", slot, cc);
        return -1;
    }
    return 0;
}

/*
 * Correct EP0's Max Packet Size once the device descriptor has been read.  The
 * slot was addressed with the core's pre-descriptor guess, which for a
 * full-speed device (8) versus the assumed 64 is simply wrong.  Evaluate
 * Context with only the EP0 add flag is the amendment the spec provides
 * (xHCI 1.1 s4.3.4). [USB-11]
 */
static int xhci_set_ep0_mps(usb_hcd_t *hcd, usb_device_t *dev, uint16_t mps)
{
    xhci_hc_t *hc = hcd->priv;
    uint8_t slot;
    uint32_t *icc, *ep0;
    int cc;

    if (mps == 0)
        return -1;

    /* Pre-SET_ADDRESS the device is still the one being enumerated. */
    slot = dev->address ? hc->addr_slot[dev->address & 0x7F] : hc->enum_slot;
    if (slot == 0 || slot > XHCI_MAX_SLOTS || !hc->slots[slot].in_ctx)
        return -1;

    mutex_lock(&hc->submit_lock);

    icc = (uint32_t *)in_ctrl_of(hc->slots[slot].in_ctx);
    icc[0] = 0;
    icc[1] = 0x2;               /* A1: EP0 context only */

    ep0 = (uint32_t *)in_ep_of(hc, hc->slots[slot].in_ctx, 1);
    ep0[1] = (ep0[1] & ~(0xFFFFu << XHCI_EP_MPS_SHIFT)) |
             ((uint32_t)mps << XHCI_EP_MPS_SHIFT);

    cc = xhci_run_command(hc, hc->slots[slot].in_ctx_dma,
                          XHCI_TRB_TYPE(TRB_EVAL_CONTEXT) |
                          ((uint32_t)slot << 24), NULL);

    icc[1] = 0x3;               /* restore slot + EP0 for the transfer path */
    mutex_unlock(&hc->submit_lock);

    if (cc != XHCI_CC_SUCCESS) {
        kprintf("xhci: setting EP0 max packet %u on slot %u failed (cc=%d)\n",
                mps, slot, cc);
        return -1;
    }
    return 0;
}

/* ---- transfers ---- */
static int xhci_control(xhci_hc_t *hc, usb_transfer_t *xfer)
{
    /* The ROOT port, not the device's own port number: for anything behind a
     * hub those differ, and the slot context wants the root one (the rest of
     * the path is the Route String). [USB-01] */
    uint8_t port = usb_root_port(xfer->dev);
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
    /* Honour the caller's timeout.  A HID poll asks to give up in a few
     * milliseconds; making it wait out the bulk timeout instead put the USB
     * thread in a permanent 1-second stall per idle poll. [USB-09] */
    int cc = xhci_wait_transfer(hc, slot, 1, &evst,
                                xfer->timeout_ms ? xfer->timeout_ms
                                                 : XHCI_CMD_TIMEOUT_MS);
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
    int cc = xhci_wait_transfer(hc, slot, dci, &evst,   /* [DRV-03] this EP's DCI */
                                xfer->timeout_ms ? xfer->timeout_ms
                                                 : XHCI_CMD_TIMEOUT_MS); /* [USB-09] */
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

/*
 * Take the controller away from the firmware.
 *
 * Real BIOSes keep the xHCI owned by SMM so USB keyboards work in setup and in
 * a legacy boot loader.  Until the OS claims ownership through the USB Legacy
 * Support extended capability, the SMI handler is still driving the same
 * registers we are — which shows up as descriptor reads that fail at random or
 * a controller that resets underneath us.  QEMU never asserts the BIOS
 * semaphore, so this path only ever does anything on hardware.
 *
 * Must run before xhci_reset(): resetting a controller the BIOS still owns is
 * exactly the race we are trying to close.
 */
static void xhci_take_controller(xhci_hc_t *hc)
{
    uint32_t hcc = rd32(hc->mmio, XHCI_CAP_HCCPARAMS1);
    uint32_t off = XHCI_HCC1_XECP(hcc) * 4;

    /* Each entry needs USBLEGSUP and USBLEGCTLSTS to be inside the mapping. */
    while (off != 0 && off + 8 <= XHCI_MMIO_SIZE) {
        uint32_t cap = rd32(hc->mmio, off);

        if (cap == 0xFFFFFFFFu)
            break;
        if (XHCI_XECP_ID(cap) == XHCI_ECAP_ID_LEGACY) {
            volatile uint8_t *bios_sem = hc->mmio + off + XHCI_LEGSUP_BIOS_SEM;
            volatile uint8_t *os_sem   = hc->mmio + off + XHCI_LEGSUP_OS_SEM;
            uint32_t ctl;

            if (*bios_sem) {
                kprintf("xhci: waiting for the BIOS to release the controller\n");
                *os_sem = 1;
                for (int i = 0; i < 5000 && *bios_sem; i++)
                    xhci_delay_ms(1);
                if (*bios_sem)
                    kprintf("xhci: BIOS never released the controller; "
                            "claiming it anyway\n");
            }

            /*
             * Silence the firmware's SMI sources and acknowledge whatever is
             * pending.  Leaving them armed lets SMM re-enter on every event we
             * generate.  The RsvdP fields have to be written back unchanged;
             * the status bits at 31:29 are write-1-to-clear.
             */
            ctl = rd32(hc->mmio, off + XHCI_LEGCTLSTS);
            ctl &= XHCI_LEGCTL_RSVD;
            ctl |= XHCI_LEGCTL_SMI_EVENTS;
            wr32(hc->mmio, off + XHCI_LEGCTLSTS, ctl);
        }
        if (XHCI_XECP_NEXT(cap) == 0)
            break;
        off += XHCI_XECP_NEXT(cap) * 4;
    }
}

/*
 * Move the shared ports from the companion EHCI to this controller.
 *
 * On Intel PCHs the USB2 ports are muxed: unless software flips XUSB2PR they
 * stay on EHCI and the xHCI sees dead root ports, so a USB 2.0 keyboard on a
 * shared port never appears here.  The *PRM registers report which ports are
 * switchable, so writing the mask routes everything the chipset permits and
 * leaves fixed ports alone.  Parts with nothing to switch read the masks as
 * zero and both writes become no-ops.
 *
 * Gated on the vendor ID: 0xD0-0xDC is vendor-specific config space and means
 * something else entirely on a non-Intel controller.
 */
/*
 * Take every companion EHCI controller away from the BIOS before the reroute.
 *
 * Setting a controller's OS-owned semaphore is a request the firmware services
 * in SMM.  If the USB2 ports have already been switched to the xHCI, that SMI
 * handler goes looking for the keyboard it was emulating on an EHCI whose ports
 * no longer exist -- and on a Lenovo C460 it never comes back.  The CPU stays
 * in SMM, so timer interrupts stop, get_uptime_ms() freezes, and the "bounded"
 * wait in ehci_take_controller() spins forever.  The observable is a boot that
 * stops dead on the line announcing the wait.
 *
 * Linux avoids this by ordering: quirk_usb_early_handoff() claims every USB
 * controller at PCI-enumeration time, and usb_enable_intel_xhci_ports() only
 * runs afterwards.  Neither driver-attach order reproduces that on its own --
 * EHCI-first hands the BIOS over in time but then enumerates devices the
 * reroute is about to remove, and xHCI-first reroutes while the BIOS still owns
 * the EHCI.  So the handoff has to happen here, before the switch, for the
 * controllers that are about to lose their ports.
 *
 * The EHCI driver still performs its own handoff when it attaches; by then the
 * semaphore is already clear and it is a no-op.
 */
static void xhci_release_companion_ehci(void)
{
    for (pci_device_t *d = pci_first_device(); d; d = pci_next_device(d)) {
        volatile uint8_t *mmio;
        uint32_t bar0, hcc;
        uint8_t off;

        /*
         * pci_device_t.class_code is (class << 8) | subclass and carries NO
         * prog-if (sys/kern/pci.c:166), so the interface byte has to come from
         * config space.  Matching on a 24-bit class here made this whole walk
         * dead code -- it never selected a single device, which is why no
         * "releasing companion EHCI" line ever appeared.
         */
        if (d->class_code != 0x0C03u)
            continue;                        /* not a USB controller */
        if (pci_read_config8(d->bus, d->slot, d->func, 0x09) != 0x20)
            continue;                        /* prog-if 0x20 = EHCI */

        bar0 = pci_read_config32(d->bus, d->slot, d->func, 0x10);
        if ((bar0 & 1) || (bar0 & ~0xFUL) == 0)
            continue;                       /* not an MMIO BAR */

        /* Memory space has to be decoding before HCCPARAMS can be read. */
        {
            uint16_t cmd = pci_read_config16(d->bus, d->slot, d->func,
                                             PCI_CONFIG_COMMAND);
            pci_write_config16(d->bus, d->slot, d->func, PCI_CONFIG_COMMAND,
                               cmd | 0x0002);
        }

        mmio = ioremap((uintptr_t)(bar0 & ~0xFUL), 0x100);
        if (!mmio)
            continue;
        hcc = *(volatile uint32_t *)(mmio + 0x08);   /* EHCI_CAP_HCCPARAMS */
        off = (uint8_t)((hcc >> 8) & 0xFF);          /* EECP */
        iounmap((void *)mmio);

        for (int guard = 0; off >= 0x40 && guard < 32; guard++) {
            uint32_t cap = pci_read_config32(d->bus, d->slot, d->func, off);
            if (cap == 0xFFFFFFFFu)
                break;
            if ((cap & 0xFF) == 0x01) {     /* USB Legacy Support */
                uint8_t sem = pci_read_config8(d->bus, d->slot, d->func, off + 2);
                if (sem) {
                    kprintf("xhci: releasing companion EHCI %02x:%02x.%u from "
                            "the BIOS before rerouting its ports\n",
                            d->bus, d->slot, d->func);
                    pci_write_config8(d->bus, d->slot, d->func, off + 3, 1);
                    for (int i = 0; i < 1000; i++) {
                        sem = pci_read_config8(d->bus, d->slot, d->func, off + 2);
                        if (sem == 0)
                            break;
                        xhci_delay_ms(1);
                    }
                    if (sem) {
                        kprintf("xhci: companion EHCI did not release; "
                                "clearing its semaphore\n");
                        pci_write_config8(d->bus, d->slot, d->func, off + 2, 0);
                    }
                }
                /* Disarm its SMI sources so the reroute cannot re-enter SMM. */
                pci_write_config32(d->bus, d->slot, d->func, off + 4, 0);
            }
            off = (uint8_t)((cap >> 8) & 0xFF);
        }
    }
}

static void xhci_intel_port_switch(pci_device_t *pdev)
{
    uint32_t mask;

    if (pdev->vendor_id != 0x8086)
        return;

    /* The companions must be OS-owned before their ports move. */
    xhci_release_companion_ehci();

    mask = pci_read_config32(pdev->bus, pdev->slot, pdev->func, XHCI_INTEL_USB3PRM);
    if (mask != 0xFFFFFFFFu && mask != 0) {
        kprintf("xhci: enabling SuperSpeed on Intel USB3 ports 0x%x\n",
                (unsigned)mask);
        pci_write_config32(pdev->bus, pdev->slot, pdev->func,
                           XHCI_INTEL_USB3_PSSEN, mask);
    }

    mask = pci_read_config32(pdev->bus, pdev->slot, pdev->func, XHCI_INTEL_USB2PRM);
    if (mask != 0xFFFFFFFFu && mask != 0) {
        kprintf("xhci: routing Intel USB2 ports 0x%x to xHCI\n", (unsigned)mask);
        pci_write_config32(pdev->bus, pdev->slot, pdev->func,
                           XHCI_INTEL_XUSB2PR, mask);
        kprintf("xhci: USB2 ports routed\n");
    }
}

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

/*
 * Bring-up trace.  Every step below touches controller MMIO and can wedge on
 * firmware-configured hardware in ways no emulator reproduces, so each one
 * announces itself: a boot that stops here names the register access that did
 * it instead of just going quiet.  Gated on "xhcidebug" so normal boots stay
 * quiet.
 */
static int xhci_trace;
#define XHCI_STEP(msg) do { if (xhci_trace) kprintf("xhci: " msg "\n"); } while (0)

static int xhci_start(xhci_hc_t *hc)
{
    XHCI_STEP("resetting controller");
    if (xhci_reset(hc) != 0) { kprintf("xhci: reset timeout\n"); return -1; }
    XHCI_STEP("reset complete");

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

    XHCI_STEP("rings allocated");

    hc->bounce = dma_alloc_coherent(XHCI_BOUNCE_SIZE, &hc->bounce_dma);
    if (!hc->bounce) return -1;

    XHCI_STEP("starting controller");
    wr32(hc->op, XHCI_OP_USBCMD, XHCI_CMD_RUN);
    for (int i = 0; i < 100 && (rd32(hc->op, XHCI_OP_USBSTS) & XHCI_STS_HCH); i++)
        xhci_delay_ms(1);
    XHCI_STEP("controller running");
    return 0;
}

/*
 * Assert PP on every port.  Idempotent, and run a second time after the
 * Intel reroute so that ports which only just became ours get powered too.
 */
static void xhci_power_ports(xhci_hc_t *hc)
{
    for (uint8_t p = 1; p <= hc->nports; p++) {
        uint32_t psc = portsc_rd(hc, p) & ~XHCI_PORT_CHANGE_MASK;
        portsc_wr(hc, p, psc | XHCI_PORT_PP);
    }
    xhci_delay_ms(20);
    XHCI_STEP("ports powered");
}

/*
 * Release everything xhci_start() may have acquired, then the controller
 * state itself.  Needed because each controller is now a separate heap
 * allocation: dropping the xhci_hc_t on a failed attach would otherwise
 * strand its DMA rings with no pointer left to free them through.
 */
static void xhci_teardown(xhci_hc_t *hc)
{
    if (hc->bounce)
        dma_free_coherent(hc->bounce, XHCI_BOUNCE_SIZE);
    if (hc->erst)
        dma_free_coherent(hc->erst, 64);
    if (hc->event_ring)
        dma_free_coherent(hc->event_ring,
                          XHCI_RING_TRBS * sizeof(struct xhci_trb));
    if (hc->cmd_ring.trb)
        dma_free_coherent(hc->cmd_ring.trb,
                          XHCI_RING_TRBS * sizeof(struct xhci_trb));
    if (hc->dcbaa)
        dma_free_coherent(hc->dcbaa, (XHCI_MAX_SLOTS + 1) * 8);
    if (hc->mmio)
        iounmap((void *)hc->mmio);
    kfree(hc, sizeof(*hc));
}

static int xhci_pci_attach(struct device *dev)
{
    pci_device_t *pdev = pci_find_device_by_kdev(dev);
    if (!pdev) return -1;

    int bt = pci_bar_type(pdev, 0);
    if (bt != PCI_BAR_MEM32 && bt != PCI_BAR_MEM64) return -1;
    uint32_t bar0 = pci_read_config32(pdev->bus, pdev->slot, pdev->func, 0x10);
    uintptr_t phys = bar0 & ~0xFUL;

    uint16_t cmd = pci_read_config16(pdev->bus, pdev->slot, pdev->func, PCI_CONFIG_COMMAND);
    pci_write_config16(pdev->bus, pdev->slot, pdev->func, PCI_CONFIG_COMMAND,
                       cmd | 0x0002 | 0x0004);

    xhci_hc_t *hc = kzalloc(sizeof(*hc));
    if (!hc) {
        kprintf("xhci: out of memory allocating controller state\n");
        return -1;
    }
    hc->mmio = ioremap(phys, XHCI_MMIO_SIZE);
    if (!hc->mmio) {
        kprintf("xhci: ioremap failed\n");
        xhci_teardown(hc);
        return -1;
    }
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

    /*
     * The BIOS handoff has to come first: it settles who owns the controller,
     * and we must own it before we reset it.
     */
    if (!cmdline_has("nousbhandoff"))
        xhci_take_controller(hc);

    if (xhci_start(hc) != 0) {
        xhci_teardown(hc);
        return -1;
    }

    /*
     * Only now hand the USB2 ports over.  Rerouting them while the controller
     * is halted wedges Lynx Point: the write lands on a controller that is not
     * running, and the machine stops on the subsequent USBCMD.RUN.  FreeBSD
     * reroutes at the very end of xhci_start_controller() for the same reason
     * (sys/dev/usb/controller/xhci.c, after the run-timeout loop), so do it
     * here, once the controller is confirmed out of HCH.  Ports that only just
     * became ours have not been powered yet, hence the second PP pass.
     *
     * Separately defeatable from the command line: it pokes firmware-owned
     * state that cannot be exercised under emulation, so a machine it wedges
     * has to stay bootable.
     */
    xhci_power_ports(hc);
    if (!cmdline_has("noxhciroute")) {
        xhci_intel_port_switch(pdev);
        xhci_power_ports(hc);
    }

    snprintf(hc->name, sizeof(hc->name), "xhci%u", xhci_instances);

    hc->hcd.priv = hc;
    hc->hcd.name = hc->name;
    hc->hcd.hcd_index = xhci_instances;
    hc->hcd.nports = hc->nports;
    hc->hcd.submit = xhci_submit;
    hc->hcd.port_status = xhci_port_status;
    hc->hcd.port_reset = xhci_port_reset;
    hc->hcd.port_enable = xhci_port_enable;
    hc->hcd.set_hub = xhci_set_hub;
    hc->hcd.set_ep0_mps = xhci_set_ep0_mps;
    usb_register_hcd(&hc->hcd);
    hc->initialized = 1;
    xhci_instances++;
    kprintf("xhci: %s: USB 3.x controller at 0x%x, %u ports, %u slots, ctx=%u\n",
            hc->name, (unsigned)phys, hc->nports, hc->maxslots, hc->ctx_size);
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
    xhci_trace = cmdline_has("xhcidebug");
    driver_register(&xhci_pci_driver, &pci_bus_type);
}
