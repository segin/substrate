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
/* Reset-path waits.  Generous rather than tight: a controller handed over by
 * UEFI firmware with transfers in flight takes noticeably longer to quiesce
 * than one nothing has touched. */
#define XHCI_CNR_WAIT_MS    1000
#define XHCI_HALT_WAIT_MS   1000
#define XHCI_RESET_WAIT_MS  1000
/* Bulk streams (USB 3.0 / UAS): MaxPStreams=1 -> 4 stream contexts, IDs 0-3
 * (0 reserved).  A stream-less serial UAS uses stream ID 1. */
#define XHCI_MAXP_STREAMS  1
#define XHCI_NUM_STREAMS   4
/* Sanity bound on the controller's scratchpad request (spec allows up to
 * 1023).  Real parts ask for a handful; anything wilder is a bad register
 * read, and we would rather fail the attach loudly than try to find megabytes
 * of contiguous memory during boot. */
#define XHCI_MAX_SCRATCHPADS 128

/*
 * Ring memory is shared with the controller, so every access to it goes
 * through a volatile pointer: these are not ordinary variables the compiler
 * may cache, fold or reorder among themselves.  The ownership handoff in
 * particular depends on the cycle bit becoming visible *after* the rest of
 * the TRB, which xhci_trb_commit() enforces. [X-11]
 */
struct xhci_ring {
    volatile struct xhci_trb *trb;
    dma_addr_t       dma;
    uint32_t         enq;      /* enqueue index */
    uint8_t          cycle;    /* producer cycle state */
};

/* Publish a TRB the controller may consume the instant the cycle bit flips.
 * x86 will not reorder the stores, but nothing stops the compiler from
 * sinking the payload writes past the control write, so state the dependency
 * rather than relying on how this happens to be compiled today.  Both BSDs
 * bracket their ring updates with an explicit sync for the same reason. */
static inline void xhci_trb_commit(volatile struct xhci_trb *t, uint32_t ctrl)
{
    __asm__ volatile("" ::: "memory");
    t->control = ctrl;
    __asm__ volatile("" ::: "memory");
}

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
    /* Protocol major revision per root port (2 or 3), from the Supported
     * Protocol capability; 0 = the capability said nothing about it. */
    uint8_t  port_major[USB_MAX_ROOT_PORTS];
    uint32_t ctx_size;         /* 32 or 64 */
    int      initialized;

    mutex_t  submit_lock;

    uint64_t *dcbaa;   dma_addr_t dcbaa_dma;
    /* Scratchpad: the controller's own scratch memory, which we own but must
     * never touch.  nscratch == 0 on a part that asks for none. */
    uint32_t  nscratch;
    uint32_t  scratch_pagesize;
    uint64_t *scratch_arr;  dma_addr_t scratch_arr_dma;   /* pointer array */
    void     *scratch_buf;  dma_addr_t scratch_buf_dma;   /* the pages */
    struct xhci_ring cmd_ring;
    volatile struct xhci_trb *event_ring;  dma_addr_t event_ring_dma;
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
    memset((void *)r->trb, 0, XHCI_RING_TRBS * sizeof(struct xhci_trb));
    /* Last TRB is a Link back to the start (toggle cycle). */
    volatile struct xhci_trb *link = &r->trb[XHCI_RING_TRBS - 1];
    link->param = r->dma;
    xhci_trb_commit(link, XHCI_TRB_TYPE(TRB_LINK) | XHCI_TRB_TC);
    r->enq = 0;
    r->cycle = 1;
    return 0;
}

/* Push a TRB (param/status/type+flags) onto a ring; returns the phys addr of
 * the slot it landed in.  Handles the trailing Link TRB + cycle toggle. */
static uint64_t xhci_ring_push(struct xhci_ring *r, uint64_t param, uint32_t status,
                               uint32_t ctrl_type_flags)
{
    volatile struct xhci_trb *t = &r->trb[r->enq];
    uint64_t phys = r->dma + (dma_addr_t)r->enq * sizeof(struct xhci_trb);
    t->param = param;
    t->status = status;
    /* set the type/flags, then the cycle bit last (ownership handoff) */
    xhci_trb_commit(t, ctrl_type_flags | (r->cycle ? XHCI_TRB_CYCLE : 0));

    r->enq++;
    if (r->enq == XHCI_RING_TRBS - 1) {
        /* hit the Link TRB: flip its cycle to hand it to the controller */
        volatile struct xhci_trb *link = &r->trb[XHCI_RING_TRBS - 1];
        xhci_trb_commit(link, (link->control & ~XHCI_TRB_CYCLE) |
                              (r->cycle ? XHCI_TRB_CYCLE : 0));
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
        volatile struct xhci_trb *e = &hc->event_ring[hc->event_deq];
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

/*
 * Recover a command ring whose command never completed (xHCI 1.2 s4.6.1.2).
 *
 * Returning from a command timeout without doing this leaves the TRB on the
 * ring with the controller still running it, while our enqueue pointer has
 * already moved past.  The next command is then issued behind a command the
 * controller may still be chewing on, and when the stale completion finally
 * arrives it is matched to the wrong request -- so a single slow command
 * poisons every command for the rest of the session.
 *
 * Setting CA asks the controller to stop; CRR clearing is it confirming it
 * has.  Then the ring is empty by definition, so reset our own producer state
 * and republish CRCR with a fresh cycle.  NetBSD's xhci_abort_command() does
 * exactly this.
 */
static void xhci_abort_command(xhci_hc_t *hc)
{
    int i;

    kprintf("xhci: command timeout; aborting the command ring\n");
    /* Only the low dword, and only to set CA: the Command Ring Pointer field
     * reads as zero and is ignored while CRR is set, so writing a pointer
     * here would be meaningless.  It is republished below once CRR clears. */
    wr32(hc->op, XHCI_OP_CRCR, rd32(hc->op, XHCI_OP_CRCR) | XHCI_CRCR_CA);
    for (i = 0; i < 500; i++) {
        if (!(rd32(hc->op, XHCI_OP_CRCR) & XHCI_CRCR_CRR))
            break;
        xhci_delay_ms(1);
    }
    if (rd32(hc->op, XHCI_OP_CRCR) & XHCI_CRCR_CRR)
        kprintf("xhci: command ring still running after abort\n");

    /* Rewind to the top of the ring: nothing on it is ours any more. */
    memset((void *)hc->cmd_ring.trb, 0,
           XHCI_RING_TRBS * sizeof(struct xhci_trb));
    volatile struct xhci_trb *link = &hc->cmd_ring.trb[XHCI_RING_TRBS - 1];
    link->param = hc->cmd_ring.dma;
    link->control = XHCI_TRB_TYPE(TRB_LINK) | XHCI_TRB_TC;
    hc->cmd_ring.enq = 0;
    hc->cmd_ring.cycle = 1;
    wr64(hc->op, XHCI_OP_CRCR, hc->cmd_ring.dma | XHCI_CRCR_RCS);
}

/* Run a command TRB on the command ring; return (completion code, slot id). */
static int xhci_run_command_st(xhci_hc_t *hc, uint64_t param, uint32_t status,
                               uint32_t ctrl, uint8_t *out_slot)
{
    xhci_ring_push(&hc->cmd_ring, param, status, ctrl);
    xhci_doorbell(hc, 0, 0);   /* ring the command doorbell */
    uint64_t ep; uint32_t ec;
    for (int guard = 0; guard < 8; guard++) {
        int cc = xhci_wait_event(hc, &ep, &ec, NULL, XHCI_CMD_TIMEOUT_MS);
        if (cc == 0) {                      /* [X-04] */
            xhci_abort_command(hc);
            return 0;
        }
        if (XHCI_TRB_GET_TYPE(ec) == TRB_CMD_COMPLETE) {
            if (out_slot) *out_slot = XHCI_TRB_GET_SLOT(ec);
            return cc;
        }
        /* skip unrelated events (e.g. port status) and keep looking */
    }
    /* Eight events came back and none was ours: the ring is out of step with
     * us, so put it back in a known state rather than pressing on. */
    xhci_abort_command(hc);
    return 0;
}

static int xhci_run_command(xhci_hc_t *hc, uint64_t param, uint32_t ctrl,
                            uint8_t *out_slot)
{
    return xhci_run_command_st(hc, param, 0, ctrl, out_slot);
}

/*
 * Bring a Halted endpoint back into service.
 *
 * A transfer that ends in STALL, Babble or a USB Transaction Error leaves the
 * endpoint Halted, and the controller then stops processing that endpoint's
 * transfer ring completely: no further TD ever runs and no event is ever
 * raised, so every subsequent transfer simply times out.  One STALL therefore
 * kills the endpoint for the rest of the session.
 *
 * usb_msc already sends CLEAR_FEATURE(ENDPOINT_HALT) to the *device*, which is
 * all EHCI and UHCI need -- their halt state lives in the device.  xHCI also
 * keeps the state in the controller, and only Reset Endpoint clears it, which
 * is why the same card reader recovers on EHCI and wedges on xHCI.
 *
 * Reset Endpoint leaves the dequeue pointer on the TD that failed, so follow
 * it with Set TR Dequeue Pointer aimed at where the next TD will be written,
 * otherwise the controller re-runs the failed transfer as soon as the doorbell
 * rings.  The Dequeue Cycle State must match the ring's current cycle.
 */
/*
 * Point the controller's dequeue pointer at where the next TD will be written.
 * Both Reset Endpoint and Stop Endpoint leave it on the TD that just went
 * wrong, so without this the controller re-runs that TD the moment the
 * doorbell rings.  The Dequeue Cycle State in bit 0 has to match the ring's
 * current producer cycle.
 */
static int xhci_set_tr_dequeue(xhci_hc_t *hc, uint8_t slot, int dci,
                               struct xhci_ring *ring, uint16_t stream_id)
{
    uint32_t ep_field = ((uint32_t)slot << 24) | ((uint32_t)dci << 16);
    uint64_t deq = (uint64_t)ring->dma +
                   (uint64_t)ring->enq * sizeof(struct xhci_trb);
    int cc = xhci_run_command_st(hc, deq | (ring->cycle ? 1u : 0u),
                                 (uint32_t)stream_id << 16,
                                 XHCI_TRB_TYPE(TRB_SET_TR_DEQUEUE) | ep_field,
                                 NULL);
    if (cc != XHCI_CC_SUCCESS) {
        kprintf("xhci: set TR dequeue slot %u dci %d failed cc=%d\n",
                slot, dci, cc);
        return -1;
    }
    return 0;
}

static int xhci_recover_ep(xhci_hc_t *hc, uint8_t slot, int dci,
                           struct xhci_ring *ring, uint16_t stream_id)
{
    uint32_t ep_field = ((uint32_t)slot << 24) | ((uint32_t)dci << 16);
    int cc;

    cc = xhci_run_command(hc, 0,
                          XHCI_TRB_TYPE(TRB_RESET_ENDPOINT) | ep_field, NULL);
    if (cc != XHCI_CC_SUCCESS) {
        kprintf("xhci: reset endpoint slot %u dci %d failed cc=%d\n",
                slot, dci, cc);
        return -1;
    }
    return xhci_set_tr_dequeue(hc, slot, dci, ring, stream_id);
}

/*
 * Abandon a transfer that never completed.
 *
 * A timeout does not halt the endpoint, so unlike the error paths above the
 * controller is still running it: our TD is on the ring, and the data buffer
 * it points at is still controller-owned.  Simply returning left that TD live.
 * With one bounce buffer shared by the whole controller, the very next
 * transfer -- for some other device, on some other endpoint -- would memcpy
 * into memory the abandoned TD could still be writing, and its late event
 * would additionally burn a slot in the next TD's event scan.
 *
 * Stop Endpoint takes the endpoint out of Running so nothing further is
 * consumed; Set TR Dequeue Pointer then moves the controller past the dead TD
 * to where the next one will be written.  Both BSDs quiesce the same way
 * before completing a transfer with an error (FreeBSD xhci_cmd_stop_ep()).
 */
static int xhci_stop_ep(xhci_hc_t *hc, uint8_t slot, int dci,
                        struct xhci_ring *ring, uint16_t stream_id)
{
    uint32_t ep_field = ((uint32_t)slot << 24) | ((uint32_t)dci << 16);
    int cc;

    cc = xhci_run_command(hc, 0,
                          XHCI_TRB_TYPE(TRB_STOP_ENDPOINT) | ep_field, NULL);
    if (cc != XHCI_CC_SUCCESS) {
        kprintf("xhci: stop endpoint slot %u dci %d failed cc=%d\n",
                slot, dci, cc);
        return -1;
    }
    return xhci_set_tr_dequeue(hc, slot, dci, ring, stream_id);
}

/* [DRV-03] Wait for the Transfer Event addressed to (slot, dci); skip unrelated
 * events (port-status changes from the hotplug scanner, other slots/endpoints).
 * Without this filter a routine Port-Status-Change event is consumed as the
 * transfer's completion, giving a bogus actual_length and desyncing every later
 * waiter.  Mirrors xhci_run_command's TRB-type filtering.  Returns the completion
 * code (0 on timeout); on a match *out_status gets the event's status dword.
 * Transfer Event control dword: [31:24] slot id, [20:16] endpoint id (DCI). */
/*
 * Wait for the Transfer Event that belongs to one specific TD.
 *
 * A Transfer Event carries, in its parameter field, the address of the TRB
 * that completed -- and that pointer is the only thing tying an event to the
 * transfer that produced it.  Matching on slot and endpoint alone is not
 * enough, because those are identical for every transfer on the endpoint: a
 * single leftover event on the ring then makes each subsequent transfer
 * complete on its predecessor's event.  Every one of them returns before its
 * own data stage has run, reports Success with residue 0, and hands back
 * whatever the previous transfer left in the shared bounce buffer.
 *
 * That is exactly what a real device hits.  Emulated devices complete with
 * no latency, so the data is already in the buffer when the driver copies it
 * and the bug is invisible -- which is why enumeration worked in QEMU with
 * emulated devices and failed on a passed-through USB 2.0 card reader, and
 * on real hardware on an xHCI-only machine.
 *
 * Checking the TRB pointer also makes the driver self-synchronising: a stale
 * event is dropped here rather than being consumed by the next transfer, so
 * one lost or duplicated event can no longer desynchronise the endpoint for
 * the rest of the session.
 */
#define XHCI_EVENT_SCAN_MAX 16

static int xhci_wait_td(xhci_hc_t *hc, uint8_t slot, int dci,
                        const uint64_t *trbs, int ntrbs,
                        uint32_t *out_status, int *out_which,
                        uint32_t timeout_ms)
{
    uint64_t ep_trb;
    uint32_t ec, est;

    for (int guard = 0; guard < XHCI_EVENT_SCAN_MAX; guard++) {
        int cc = xhci_wait_event(hc, &ep_trb, &ec, &est, timeout_ms);
        if (cc == 0) return 0;   /* timeout */

        /* Not a transfer event for this endpoint at all (port status change,
         * another slot, another EP): drop it and keep looking. */
        if (XHCI_TRB_GET_TYPE(ec) != TRB_TRANSFER_EVENT ||
            XHCI_TRB_GET_SLOT(ec) != slot ||
            (int)((ec >> 16) & 0x1F) != dci) {
            continue;
        }
        for (int i = 0; i < ntrbs; i++) {
            if (trbs[i] != 0 && ep_trb == trbs[i]) {
                if (out_status) *out_status = est;
                if (out_which) *out_which = i;
                return cc;
            }
        }
        /* Right endpoint, but an older TD's TRB: a leftover.  Drop it --
         * consuming it as ours is the bug described above. */
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
static void xhci_port_gone(usb_hcd_t *hcd, uint8_t port)
{
    xhci_hc_t *hc = hcd->priv;
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
        uint8_t major = (port >= 1 && port <= USB_MAX_ROOT_PORTS)
                        ? hc->port_major[port - 1] : 0;

        if (major == 3) {
            /* Every speed a USB3 protocol port can train at -- SuperSpeed and
             * the SuperSpeedPlus gears above it -- is SuperSpeed as far as the
             * core is concerned.  Reading the PSID against the default table
             * here is what reported a Gen 2 port as high speed. [X-10] */
            out |= USB_PORT_STAT_SUPER_SPEED;
        } else {
            switch (psid) {
            case 1: break;                                      /* full speed */
            case 2: out |= USB_PORT_STAT_LOW_SPEED;   break;
            case 3: out |= USB_PORT_STAT_HIGH_SPEED;  break;
            case 4: out |= USB_PORT_STAT_SUPER_SPEED; break;
            default:
                /* Unknown ID on a port whose protocol we never learned: at or
                 * above the SuperSpeed default is at least SuperSpeed, below
                 * it assume high speed as before. */
                out |= (psid >= 4) ? USB_PORT_STAT_SUPER_SPEED
                                   : USB_PORT_STAT_HIGH_SPEED;
                break;
            }
        }
    }
    /*
     * Nothing else.  This used to reap the departed slot here, which meant a
     * routine status poll took submit_lock and ran Disable Slot commands --
     * and a poll loop added elsewhere in this driver duly fired hundreds of
     * teardowns and broke enumeration.  The reaping now hangs off the
     * port_gone hook, called from the hot-plug scan.  Keep this a pure
     * read. [X-12]
     */
    return out;
}

/* Drive one reset of `port` using `reset_bit` (PR for a hot reset, WPR for a
 * warm one) and report whether the port came out enabled. */
static int xhci_do_reset(xhci_hc_t *hc, uint8_t port, uint32_t reset_bit)
{
    uint32_t psc = portsc_rd(hc, port) & ~XHCI_PORT_CLEAR;
    uint32_t ack;

    portsc_wr(hc, port, psc | reset_bit);

    /*
     * The reset bit self-clears when the reset finishes, and it is that 1->0
     * transition which sets the matching change flag.  Watch the reset bit:
     * it is the controller stating the reset is over, rather than a change
     * flag that may already have been latched.  NetBSD polls the same bit
     * (UHF_PORT_RESET in xhci_roothub_ctrl).
     */
    for (int i = 0; i < 100; i++) {
        xhci_delay_ms(2);
        psc = portsc_rd(hc, port);
        if (!(psc & reset_bit))
            break;
    }

    /* Acknowledge whichever change flags latched, *without* writing PED back.
     * A successful reset leaves PED set, and PED is RW1CS: carrying it into
     * the write disables the port we are in the middle of bringing up.  The
     * PED test below would then read 0 and report a failed reset, so the port
     * gets skipped and nothing on it ever enumerates.  See XHCI_PORT_CLEAR. */
    ack = psc & (XHCI_PORT_PRC | XHCI_PORT_WRC);
    if (ack)
        portsc_wr(hc, port, (psc & ~XHCI_PORT_CLEAR) | ack);

    /* Let the device finish coming out of reset before anyone talks to it.
     * usb_scan_ports() goes straight from here into enumeration, so if this
     * wait is not taken here it is not taken anywhere. [X-07] */
    xhci_delay_ms(XHCI_PORT_RESET_RECOVERY_MS);

    return (portsc_rd(hc, port) & XHCI_PORT_PED) ? 0 : -1;
}

static int xhci_port_reset(usb_hcd_t *hcd, uint8_t port)
{
    xhci_hc_t *hc = hcd->priv;
    uint32_t psc = portsc_rd(hc, port);
    if (!(psc & XHCI_PORT_CCS)) return -1;

    /* USB3 ports often auto-enable on connect; if already enabled, done. */
    if (psc & XHCI_PORT_PED)
        return 0;

    if (xhci_do_reset(hc, port, XHCI_PORT_PR) == 0)
        return 0;

    /*
     * A SuperSpeed port that will not come up under a hot reset can still
     * recover from a warm one, which retrains the link from scratch instead
     * of assuming it is already up: that is the way out of Inactive,
     * Compliance, or a port stuck in Polling after a failed negotiation.
     * Both BSDs expose it (FreeBSD UHF_BH_PORT_RESET -> XHCI_PS_WPR).
     * Meaningless on a USB2 port, and PSID only says SuperSpeed or better
     * once the port has trained, so require both that and a live connection.
     */
    psc = portsc_rd(hc, port);
    if ((psc & XHCI_PORT_CCS) &&
        ((psc & XHCI_PORT_SPEED_MASK) >> XHCI_PORT_SPEED_SHIFT) >= 4) {
        kprintf("xhci: port %u would not enable; trying a warm reset\n", port);
        return xhci_do_reset(hc, port, XHCI_PORT_WPR);   /* [X-15] */
    }
    return -1;
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
            dma_free_coherent((void *)s->ep_ring[dci].trb,
                              XHCI_RING_TRBS * sizeof(struct xhci_trb));
            s->ep_ring[dci].trb = NULL;
        }
        for (int sid = 0; sid < XHCI_NUM_STREAMS; sid++) {
            if (s->stream_ring[dci][sid].trb) {
                dma_free_coherent((void *)s->stream_ring[dci][sid].trb,
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
    ep0[4] = XHCI_EP_AVG_TRB_LEN(XHCI_EP_AVG_TRB_CTRL);   /* [X-05] */

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
/*
 * Endpoint service interval, xHCI encoding (spec 6.2.3.6).
 *
 * The controller polls a periodic endpoint only as often as its Interval
 * field says; leaving the field at zero is not "poll as fast as possible",
 * it is an out-of-range value for a full-speed interrupt endpoint, and the
 * reports simply never arrive.  A keyboard then enumerates perfectly and
 * types nothing -- which is exactly how this presented.
 *
 * The units differ by speed.  Low/full speed report bInterval in 1 ms
 * frames, and xHCI wants a 125 us exponent, valid range 3..10.  High and
 * super speed already report an exponent (1..16), so it is bInterval-1.
 * Bulk and control endpoints are asynchronous and take 0.
 */
static uint32_t xhci_ep_interval(const usb_device_t *dev, const usb_endpoint_t *ep)
{
    uint8_t bi;
    uint32_t iv;

    if (!ep || ep->type != USB_EP_TYPE_INTERRUPT)
        return 0;

    bi = ep->interval ? ep->interval : 1;
    if (!dev || dev->speed == USB_SPEED_LOW || dev->speed == USB_SPEED_FULL) {
        /*
         * Round DOWN, not up.  Table 6-12 footnote 113: "For FS/LS Interrupt
         * endpoints software shall round the computed value of Endpoint
         * Context Interval field down to the nearest base 2 multiple of
         * bInterval * 8."  Rounding up polls at half the requested rate for
         * any bInterval that is not already a power of two -- a mouse asking
         * for 10ms was serviced every 16ms.  Valid range for this form is
         * 3..10.
         */
        uint32_t units = (uint32_t)bi * 8u;     /* bInterval in 125us units */
        for (iv = 3; iv < 10 && (1u << (iv + 1)) <= units; iv++)
            ;
        return iv;
    }
    iv = (uint32_t)bi - 1u;
    return iv > 15u ? 15u : iv;
}

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
    /*
     * Carry the transaction-translator fields over too.  in_ctx was just
     * memset, so anything not copied back from the device context is zero --
     * and dword 2 is where xhci_setup_slot put the TT Hub Slot ID and TT Port
     * Number for a low/full-speed device sitting behind a high-speed hub.
     * Dropping them here tells the controller there is no TT in the path, so
     * the endpoint we are configuring gets scheduled as if the device were
     * high-speed.  EP0 keeps working (it was addressed with the fields intact)
     * and only the endpoint added by this command misbehaves, which is why it
     * looks like a device that enumerates fine and then goes quiet. [X-06]
     */
    insc[2] = dsc[2];

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
        /* Write DW0 rather than inheriting it: the input context is reused
         * across Configure Endpoint commands, so a previously-streamed DCI
         * would leave MaxPStreams/LSA set and the controller would read the
         * TR-dequeue field below as a stream-context-array base. */
        epc[0] = xhci_ep_interval(xfer->dev, xfer->ep) << XHCI_EP_INTERVAL_SHIFT;
        epc[2] = (uint32_t)s->ep_ring[dci].dma | s->ep_ring[dci].cycle;
        epc[3] = (uint32_t)((uint64_t)s->ep_ring[dci].dma >> 32);
    }
    /*
     * Bandwidth parameters.  A periodic endpoint moves at most one max-packet
     * per service interval here (we set neither Mult nor Max Burst), so that
     * is its Max ESIT Payload; an async endpoint reserves nothing and only
     * needs a representative TRB length. [X-05]
     */
    if (xfer->ep->type == USB_EP_TYPE_INTERRUPT) {
        uint32_t avg = mps < XHCI_EP_AVG_TRB_BULK ? mps : XHCI_EP_AVG_TRB_BULK;
        epc[4] = XHCI_EP_AVG_TRB_LEN(avg) | XHCI_EP_MAX_ESIT_LO(mps);
    } else {
        epc[4] = XHCI_EP_AVG_TRB_LEN(XHCI_EP_AVG_TRB_BULK);
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
    /* Default to failure so the several early returns below cannot leave the
     * caller reading whatever the previous transfer left here.  Every path
     * that actually succeeds overwrites it. [X-16] */
    xfer->status = USB_XFER_ERROR;
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
        /* A device is allowed a settling period after being addressed before
         * it has to answer on the new address (USB 2.0 s9.2.6.3). [X-07] */
        xhci_delay_ms(XHCI_SET_ADDRESS_SETTLE_MS);
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
    /* Remember each stage's TRB address: the completion is identified by
     * which of them the event points at (see xhci_wait_td). */
    uint64_t td[3];
    int ntd = 0;

    td[ntd++] = xhci_ring_push(ring, setup_param, 8,
                   XHCI_TRB_TYPE(TRB_SETUP) | XHCI_TRB_IDT | trt);
    if (len) {
        /* ISP: a device that answers with less than we asked for completes
         * the data stage early, and without this that raises no event at
         * all -- the status stage then reports residue 0 and the short read
         * is indistinguishable from a full one. */
        td[ntd++] = xhci_ring_push(ring, hc->bounce_dma, len,
                       XHCI_TRB_TYPE(TRB_DATA) | XHCI_TRB_ISP |
                       (in ? TRB_DIR_IN : 0));
    }
    /* Status stage: opposite direction, IOC so we get a Transfer Event. */
    uint32_t status_dir = (in && len) ? 0 : TRB_DIR_IN;
    td[ntd++] = xhci_ring_push(ring, 0, 0,
                   XHCI_TRB_TYPE(TRB_STATUS) | status_dir | XHCI_TRB_IOC);

    xhci_doorbell(hc, slot, 1);   /* DCI 1 = EP0 */

    /* Honour the caller's timeout.  A HID poll asks to give up in a few
     * milliseconds; making it wait out the bulk timeout instead put the USB
     * thread in a permanent 1-second stall per idle poll. [USB-09] */
    uint32_t tmo = xfer->timeout_ms ? xfer->timeout_ms : XHCI_CMD_TIMEOUT_MS;
    uint32_t evst = 0;
    uint32_t residue = 0;
    int cc;

    /*
     * The transfer is done when the STATUS stage completes.  A short data
     * stage reports first (ISP) and is the only place the real residue
     * appears -- the status event's length is always 0 -- so take it and
     * keep waiting rather than returning, which would leave the status
     * event on the ring for the next transfer to trip over.
     */
    for (;;) {
        int which = -1;
        cc = xhci_wait_td(hc, slot, 1, td, ntd, &evst, &which, tmo);
        if (cc != XHCI_CC_SUCCESS && cc != XHCI_CC_SHORT_PACKET) {
            if (XHCI_CC_HALTS_EP(cc))
                (void)xhci_recover_ep(hc, slot, 1, ring, 0);
            else if (cc == 0)
                (void)xhci_stop_ep(hc, slot, 1, ring, 0);  /* [X-03] */
            xfer->status = (cc == 0) ? USB_XFER_TIMEOUT : USB_XFER_STALL;
            return xfer->status;
        }
        if (len && which == 1) {           /* data stage, short */
            residue = XHCI_TRB_GET_XLEN(evst);
            continue;
        }
        break;                             /* status stage: transfer complete */
    }
    xfer->actual_length = (len > residue) ? (len - residue) : len;
    if (in && len) memcpy(xfer->data, hc->bounce, xfer->actual_length);
    xfer->status = USB_XFER_OK;
    (void)addr;
    return USB_XFER_OK;
}

static int xhci_bulk(xhci_hc_t *hc, usb_transfer_t *xfer)
{
    xfer->status = USB_XFER_ERROR;          /* [X-16], see xhci_control() */
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

    uint64_t td[1];
    td[0] = xhci_ring_push(ring, len ? hc->bounce_dma : 0, len,
                   XHCI_TRB_TYPE(TRB_NORMAL) | XHCI_TRB_IOC | XHCI_TRB_ISP);
    xhci_doorbell(hc, slot, db_target);

    uint32_t evst;
    int cc = xhci_wait_td(hc, slot, dci, td, 1, &evst,  /* [DRV-03] this EP's DCI */
                          NULL,
                          xfer->timeout_ms ? xfer->timeout_ms
                                           : XHCI_CMD_TIMEOUT_MS); /* [USB-09] */
    if (cc != XHCI_CC_SUCCESS && cc != XHCI_CC_SHORT_PACKET) {
        /* Clear the controller-side halt before returning, so the class
         * driver's retry (after its own CLEAR_FEATURE) has a live endpoint
         * to retry on rather than timing out forever. */
        uint16_t sid = (xfer->ep->max_streams > 0)
                       ? (xfer->stream_id ? xfer->stream_id : 1) : 0;
        if (XHCI_CC_HALTS_EP(cc))
            (void)xhci_recover_ep(hc, slot, dci, ring, sid);
        else if (cc == 0)
            (void)xhci_stop_ep(hc, slot, dci, ring, sid);   /* [X-03] */
        xfer->status = (cc == 0) ? USB_XFER_TIMEOUT : USB_XFER_STALL;
        return xfer->status;
    }

    uint32_t residue = XHCI_TRB_GET_XLEN(evst);
    xfer->actual_length = (len > residue) ? (len - residue) : len;
    if (in && xfer->actual_length) memcpy(xfer->data, hc->bounce, xfer->actual_length);
    xfer->status = USB_XFER_OK;
    return USB_XFER_OK;
}

/*
 * Report a controller that has faulted, and drop any events nobody claimed.
 *
 * The event ring is only ever read while a transfer or command is in flight,
 * so Port Status Change events -- which the controller posts on every port
 * transition and which no code path here consumes -- just pile up.  The ring
 * is 64 entries; once it fills, the controller can no longer post the
 * Transfer Events we are waiting for and every transfer times out with no
 * clue as to why.  A flapping port or a marginal cable is enough to get there.
 *
 * USBSTS is checked at the same time because a controller that has taken a
 * Host System Error or Host Controller Error otherwise looks exactly like one
 * that is merely slow.  FreeBSD reports both (xhci_interrupt).
 */
static void xhci_drain_events(xhci_hc_t *hc)
{
    uint32_t sts = rd32(hc->op, XHCI_OP_USBSTS);
    int drained = 0;

    if (sts & (XHCI_STS_HSE | XHCI_STS_HCE | XHCI_STS_HCH)) {
        kprintf("xhci: %s: controller fault (usbsts=0x%x%s%s%s)\n",
                hc->name, (unsigned)sts,
                (sts & XHCI_STS_HCH) ? " halted" : "",
                (sts & XHCI_STS_HSE) ? " host-system-error" : "",
                (sts & XHCI_STS_HCE) ? " host-controller-error" : "");
        /* HSE is RW1C; acknowledge so a single fault is not reported forever. */
        if (sts & XHCI_STS_HSE)
            wr32(hc->op, XHCI_OP_USBSTS, XHCI_STS_HSE);
    }

    /*
     * Consume whatever is already pending.  The cycle bit is tested directly
     * rather than leaning on a zero timeout, because xhci_wait_event() with
     * no budget still spins until the millisecond counter ticks -- which
     * would cost far more than the drain saves.  Bounded by the ring size so
     * a controller producing events faster than we retire them cannot trap us
     * here holding submit_lock.
     */
    while (drained < XHCI_RING_TRBS) {
        volatile struct xhci_trb *e = &hc->event_ring[hc->event_deq];
        if ((e->control & XHCI_TRB_CYCLE) != hc->event_cycle)
            break;
        (void)xhci_wait_event(hc, NULL, NULL, NULL, 0);
        drained++;
    }
}

static int xhci_submit(usb_hcd_t *hcd, usb_transfer_t *xfer)
{
    xhci_hc_t *hc = hcd->priv;
    int ret;
    mutex_lock(&hc->submit_lock);
    xhci_drain_events(hc);   /* [X-08] */
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

    /* Each entry needs USBLEGSUP and USBLEGCTLSTS to be inside the mapping.
     * Bounded iteration as well: the list is controller-supplied, and a
     * malformed next-pointer that walks backwards would otherwise loop. */
    for (int guard = 0; off != 0 && guard < 64; guard++) {
        uint32_t cap;

        if (off + 8 > XHCI_MMIO_SIZE) {
            /* Say so rather than silently giving up: stopping here means the
             * BIOS handoff below may never run, and we would then reset a
             * controller that SMM still owns. [X-09] */
            kprintf("xhci: extended capabilities continue past the mapped "
                    "window (at 0x%x); BIOS handoff may be incomplete\n",
                    (unsigned)off);
            break;
        }
        cap = rd32(hc->mmio, off);

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
static void xhci_intel_port_switch(pci_device_t *pdev)
{
    uint32_t mask;

    if (pdev->vendor_id != 0x8086)
        return;

    /*
     * No companion-EHCI handoff here.  FreeBSD (sys/dev/usb/controller/xhci.c
     * sc_port_route) and NetBSD (sys/dev/pci/xhci_pci.c xhci_pci_port_route)
     * both perform this switch as four bare config accesses and never touch
     * the EHCI.  We used to claim each companion from the BIOS first, which
     * requests service in SMM -- and on a Lenovo C460 that never returns.
     * That code was dead from the day it was written (it matched a 24-bit
     * class against a 16-bit field) so it never fixed anything; the EHCI
     * wedge it was meant to cure had already gone away on its own.
     */
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

/*
 * Reset the controller into a known state.
 *
 * Ordering matters and is not optional: HCRST may only be asserted while
 * HCHalted is set (xHCI 5.4.1).  Writing it to a running controller is
 * undefined, and on a firmware-initialised part it simply never completes --
 * which is exactly how this failed.  UEFI runs its own xHCI driver so the
 * boot keyboard works in the firmware menus, and hands the controller over
 * still running; the old code cleared RUN, waited a fixed 100 ms without
 * checking the result, then asserted HCRST regardless.  The reset timed out,
 * xhci_pci_attach gave up, and the machine had no USB at all -- while the
 * same image booted from a legacy BIOS, where nothing had touched the
 * controller, reset cleanly and worked.
 *
 * Every wait now has a real bound and says what it saw, because "reset
 * timeout" on its own does not distinguish "never halted" from "halted but
 * the reset bit never cleared", and those have different causes.
 */
static int xhci_reset(xhci_hc_t *hc)
{
    int i;

    /* Controller Not Ready: the part is still bringing itself up. */
    for (i = 0; i < XHCI_CNR_WAIT_MS &&
                (rd32(hc->op, XHCI_OP_USBSTS) & XHCI_STS_CNR); i++)
        xhci_delay_ms(1);
    if (rd32(hc->op, XHCI_OP_USBSTS) & XHCI_STS_CNR) {
        kprintf("xhci: controller not ready after %d ms (usbsts=0x%x)\n",
                XHCI_CNR_WAIT_MS, (unsigned)rd32(hc->op, XHCI_OP_USBSTS));
        return -1;
    }

    /* Stop it, and confirm it stopped before touching HCRST. */
    wr32(hc->op, XHCI_OP_USBCMD, rd32(hc->op, XHCI_OP_USBCMD) & ~XHCI_CMD_RUN);
    for (i = 0; i < XHCI_HALT_WAIT_MS &&
                !(rd32(hc->op, XHCI_OP_USBSTS) & XHCI_STS_HCH); i++)
        xhci_delay_ms(1);
    if (!(rd32(hc->op, XHCI_OP_USBSTS) & XHCI_STS_HCH)) {
        kprintf("xhci: will not halt after %d ms "
                "(usbcmd=0x%x usbsts=0x%x); not asserting HCRST\n",
                XHCI_HALT_WAIT_MS,
                (unsigned)rd32(hc->op, XHCI_OP_USBCMD),
                (unsigned)rd32(hc->op, XHCI_OP_USBSTS));
        return -1;
    }

    wr32(hc->op, XHCI_OP_USBCMD, XHCI_CMD_HCRST);
    for (i = 0; i < XHCI_RESET_WAIT_MS; i++) {
        if (!(rd32(hc->op, XHCI_OP_USBCMD) & XHCI_CMD_HCRST) &&
            !(rd32(hc->op, XHCI_OP_USBSTS) & XHCI_STS_CNR))
            return 0;
        xhci_delay_ms(1);
    }
    kprintf("xhci: reset did not complete in %d ms "
            "(usbcmd=0x%x usbsts=0x%x)\n", XHCI_RESET_WAIT_MS,
            (unsigned)rd32(hc->op, XHCI_OP_USBCMD),
            (unsigned)rd32(hc->op, XHCI_OP_USBSTS));
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

/*
 * Hand the controller its scratchpad pages.
 *
 * An xHC may require some number of PAGESIZE buffers of system memory to keep
 * its own internal state in.  This is not a hint and not an optimisation:
 * xHCI 1.2 s4.20 says software "shall allocate the Scratchpad Buffer(s) before
 * placing the xHC in to Run mode", and the controller reaches them through
 * entry 0 of the DCBAA -- the one entry in that array that names no device.
 *
 * We never allocated any, and left DCBAA[0] holding the zero that the memset
 * put there, so a controller that asked for scratch space was pointed at
 * physical address 0.  Emulated controllers request none, which is why this
 * survived: HCSPARAMS2 reads back 0 under QEMU and the whole path is skipped.
 *
 * Once handed over the pages belong to the controller -- "System software
 * shall not read or write a Scratchpad buffer" -- but software does have to
 * zero them first (s4.20 step 4b).  They are allocated as one contiguous run
 * rather than one call per page: memory is unfragmented at driver-attach time,
 * and it keeps teardown to a single free.
 */
/*
 * Record which USB protocol each root port speaks.
 *
 * The Protocol Speed IDs in PORTSC are only meaningful against a protocol:
 * xHCI 1.2 s7.2 defines a *default* mapping (1=FS, 2=LS, 3=HS, 4=SS) which a
 * controller is free to redefine per Supported Protocol block.  Without
 * reading the capability, everything above the defaults falls off the end of
 * the table -- a USB 3.1 Gen 2 port reports PSID 5 and was being handed to
 * the core as high speed.
 *
 * NetBSD parses the same capability in xhci_id_protocols(), where it also
 * builds a controller-port to root-hub-port map and splits the USB2 and USB3
 * buses.  We only need enough to interpret the speed field, so this records
 * the major revision per port and nothing more. [X-10]
 */
static void xhci_parse_protocols(xhci_hc_t *hc)
{
    uint32_t hcc = rd32(hc->mmio, XHCI_CAP_HCCPARAMS1);
    uint32_t off = XHCI_HCC1_XECP(hcc) * 4;

    for (int guard = 0; off != 0 && guard < 64; guard++) {
        uint32_t cap;

        if (off + 16 > XHCI_MMIO_SIZE)
            break;
        cap = rd32(hc->mmio, off);
        if (cap == 0xFFFFFFFFu)
            break;

        /* Dword 1 must read "USB "; anything else is a vendor protocol we
         * have no business interpreting. */
        if (XHCI_XECP_ID(cap) == XHCI_ECAP_ID_PROTOCOL &&
            rd32(hc->mmio, off + 4) == XHCI_XECP_SP_NAME_USB) {
            uint32_t w2 = rd32(hc->mmio, off + 8);
            unsigned first = XHCI_XECP_SP_PORT_OFF(w2);
            unsigned count = XHCI_XECP_SP_PORT_CNT(w2);
            unsigned major = XHCI_XECP_SP_MAJOR(cap);

            for (unsigned p = first; count && p < first + count; p++) {
                if (p >= 1 && p <= hc->nports && p <= USB_MAX_ROOT_PORTS)
                    hc->port_major[p - 1] = (uint8_t)major;
            }
            if (xhci_trace && count)
                kprintf("xhci: USB %u.x on ports %u-%u\n",
                        major, first, first + count - 1);
        }

        if (XHCI_XECP_NEXT(cap) == 0)
            break;
        off += XHCI_XECP_NEXT(cap) * 4;
    }
}

static int xhci_alloc_scratchpad(xhci_hc_t *hc)
{
    uint32_t ps, i;

    hc->nscratch = XHCI_HCS2_SPB_MAX(rd32(hc->mmio, XHCI_CAP_HCSPARAMS2));
    if (hc->nscratch == 0)
        return 0;
    if (hc->nscratch > XHCI_MAX_SCRATCHPADS) {
        kprintf("xhci: controller asks for %u scratchpad pages (max %u)\n",
                (unsigned)hc->nscratch, XHCI_MAX_SCRATCHPADS);
        return -1;
    }

    /* PAGESIZE is a bitmap of the sizes the xHC supports; use the smallest. */
    ps = rd32(hc->op, XHCI_OP_PAGESIZE) & XHCI_PAGESIZE_MASK;
    hc->scratch_pagesize = 4096;
    for (i = 0; i < 16; i++) {
        if (ps & (1u << i)) {
            hc->scratch_pagesize = 1u << (i + 12);
            break;
        }
    }

    hc->scratch_arr = dma_alloc_coherent(hc->nscratch * sizeof(uint64_t),
                                         &hc->scratch_arr_dma);
    if (!hc->scratch_arr)
        return -1;
    hc->scratch_buf = dma_alloc_coherent(hc->nscratch * hc->scratch_pagesize,
                                         &hc->scratch_buf_dma);
    if (!hc->scratch_buf)
        return -1;              /* xhci_teardown frees the array */

    /*
     * "A Scratchpad Buffer is a PAGESIZE block of system memory located on a
     * PAGESIZE boundary."  dma_alloc_coherent returns page-aligned memory,
     * which satisfies the usual 4KB case; if a controller reports a larger
     * page size than our allocator can align to, refuse rather than quietly
     * hand it a misaligned buffer.
     */
    if (hc->scratch_buf_dma & (dma_addr_t)(hc->scratch_pagesize - 1)) {
        kprintf("xhci: scratchpad not aligned to the %u-byte xHC page size\n",
                (unsigned)hc->scratch_pagesize);
        return -1;
    }

    memset(hc->scratch_buf, 0, hc->nscratch * hc->scratch_pagesize);
    for (i = 0; i < hc->nscratch; i++)
        hc->scratch_arr[i] = (uint64_t)hc->scratch_buf_dma +
                             (uint64_t)i * hc->scratch_pagesize;

    hc->dcbaa[0] = hc->scratch_arr_dma;
    return 0;
}

static int xhci_start(xhci_hc_t *hc)
{
    XHCI_STEP("resetting controller");
    if (xhci_reset(hc) != 0) { kprintf("xhci: reset timeout\n"); return -1; }
    XHCI_STEP("reset complete");

    wr32(hc->op, XHCI_OP_CONFIG, hc->maxslots);

    hc->dcbaa = dma_alloc_coherent((XHCI_MAX_SLOTS + 1) * 8, &hc->dcbaa_dma);
    if (!hc->dcbaa) return -1;
    memset(hc->dcbaa, 0, (XHCI_MAX_SLOTS + 1) * 8);

    /* Fills in dcbaa[0], so it has to happen before DCBAAP is published --
     * and, either way, before RUN is set below. */
    if (xhci_alloc_scratchpad(hc) != 0) {
        kprintf("xhci: cannot allocate scratchpad buffers\n");
        return -1;
    }
    if (hc->nscratch)
        XHCI_STEP("scratchpad allocated");

    wr64(hc->op, XHCI_OP_DCBAAP, hc->dcbaa_dma);

    if (xhci_ring_alloc(&hc->cmd_ring) != 0) return -1;
    wr64(hc->op, XHCI_OP_CRCR, hc->cmd_ring.dma | XHCI_CRCR_RCS);

    /* Event ring: one segment + a one-entry ERST. */
    hc->event_ring = dma_alloc_coherent(XHCI_RING_TRBS * sizeof(struct xhci_trb),
                                        &hc->event_ring_dma);
    hc->erst = dma_alloc_coherent(64, &hc->erst_dma);
    if (!hc->event_ring || !hc->erst) return -1;
    memset((void *)hc->event_ring, 0, XHCI_RING_TRBS * sizeof(struct xhci_trb));
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
    /*
     * Setting RUN only requests the start; HCH clearing is the controller
     * reporting it actually left the halted state.  This used to announce
     * "controller running" whether or not that ever happened, so a controller
     * that never started looked identical to one that did -- and every command
     * issued afterwards then timed out with no completion event at all
     * ("enable slot failed (cc=0)").  FreeBSD fails the attach here with a
     * "Run timeout" (sys/dev/usb/controller/xhci.c); do the same.
     */
    {
        int i;
        for (i = 0; i < 100 && (rd32(hc->op, XHCI_OP_USBSTS) & XHCI_STS_HCH); i++)
            xhci_delay_ms(1);
        if (rd32(hc->op, XHCI_OP_USBSTS) & XHCI_STS_HCH) {
            wr32(hc->op, XHCI_OP_USBCMD, 0);
            kprintf("xhci: run timeout (still halted, usbsts=0x%x)\n",
                    (unsigned)rd32(hc->op, XHCI_OP_USBSTS));
            return -1;
        }
    }
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
        uint32_t psc = portsc_rd(hc, p) & ~XHCI_PORT_CLEAR;
        portsc_wr(hc, p, psc | XHCI_PORT_PP);
    }
    /*
     * Power-on to power-good.  USB 2.0 allows a port up to 100ms before it
     * reports a connection, and that is what FreeBSD waits: its hub driver
     * sleeps bPwrOn2PwrGood after setting PORT_POWER, floored by
     * USB_PORT_POWERUP_DELAY_SPEC (usb.h), which is 100ms.
     *
     * This was 20ms, and every emulator was happy with it -- QEMU asserts CCS
     * the instant the port is powered, so the scan that follows always found
     * the device.  Real silicon does not, and there is only one synchronous
     * scan before the root filesystem is mounted, so a port that had not
     * settled yet was simply never seen: an Intel Sunrise Point laptop
     * enumerated nothing at all and panicked with no root device.
     */
    xhci_delay_ms(100);
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
    /*
     * Stop the controller before handing back memory it may still be reading.
     * A failure after xhci_start() succeeded would otherwise leave it running
     * with DCBAAP, CRCR and ERSTBA all pointing into pages we are about to
     * free.  hc->op is NULL if we never got as far as decoding CAPLENGTH. [X-17]
     */
    if (hc->op) {
        wr32(hc->op, XHCI_OP_USBCMD,
             rd32(hc->op, XHCI_OP_USBCMD) & ~XHCI_CMD_RUN);
        for (int i = 0; i < XHCI_HALT_WAIT_MS &&
                        !(rd32(hc->op, XHCI_OP_USBSTS) & XHCI_STS_HCH); i++)
            xhci_delay_ms(1);
    }
    if (hc->bounce)
        dma_free_coherent(hc->bounce, XHCI_BOUNCE_SIZE);
    if (hc->erst)
        dma_free_coherent(hc->erst, 64);
    if (hc->event_ring)
        dma_free_coherent((void *)hc->event_ring,
                          XHCI_RING_TRBS * sizeof(struct xhci_trb));
    if (hc->cmd_ring.trb)
        dma_free_coherent((void *)hc->cmd_ring.trb,
                          XHCI_RING_TRBS * sizeof(struct xhci_trb));
    if (hc->scratch_buf)
        dma_free_coherent(hc->scratch_buf,
                          hc->nscratch * hc->scratch_pagesize);
    if (hc->scratch_arr)
        dma_free_coherent(hc->scratch_arr, hc->nscratch * sizeof(uint64_t));
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
    /*
     * A 64-bit BAR is two dwords, and the upper one was being ignored: the
     * type check above accepts PCI_BAR_MEM64 but only 0x10 was ever read.
     * A legacy BIOS assigns this controller a low address, so the upper dword
     * is zero and the bug is invisible -- but UEFI firmware on a large-memory
     * guest is free to place it above 4 GiB, and then we mapped whatever the
     * low half happened to alias.  The symptom was a controller whose reset
     * never completed and whose USBSTS came back with reserved bits set,
     * i.e. not that controller's registers at all.
     */
    uint64_t phys64 = (uint64_t)(bar0 & ~0xFUL);
    if (bt == PCI_BAR_MEM64) {
        uint32_t bar1 = pci_read_config32(pdev->bus, pdev->slot, pdev->func, 0x14);
        phys64 |= (uint64_t)bar1 << 32;
    }
    if (phys64 >> 32) {
        /*
         * Above 4 GiB and unmappable here, so move it.  Firmware is free to
         * put a 64-bit BAR anywhere, and UEFI on a large-memory machine does
         * exactly that; nothing else has claimed the controller yet, so
         * reassigning it into the PCI hole is safe at this point.
         */
        kprintf("xhci: BAR0 at 0x%x%08x is above 4 GiB; relocating\n",
                (unsigned)(phys64 >> 32), (unsigned)(uint32_t)phys64);
        if (pci_relocate_bar32(pdev, 0) != 0) {
            kprintf("xhci: cannot bring BAR0 below 4 GiB; giving up\n");
            return -1;
        }
        bar0 = pci_read_config32(pdev->bus, pdev->slot, pdev->func, 0x10);
        phys64 = (uint64_t)(bar0 & ~0xFUL);
        if (bt == PCI_BAR_MEM64) {
            uint32_t hi = pci_read_config32(pdev->bus, pdev->slot, pdev->func, 0x14);
            phys64 |= (uint64_t)hi << 32;
        }
        if (phys64 >> 32)
            return -1;
    }
    uintptr_t phys = (uintptr_t)phys64;

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
    uint32_t rtsoff = rd32(hc->mmio, XHCI_CAP_RTSOFF) & ~0x1Fu;
    uint32_t dboff  = rd32(hc->mmio, XHCI_CAP_DBOFF) & ~0x3u;
    uint32_t hcs1 = rd32(hc->mmio, XHCI_CAP_HCSPARAMS1);
    hc->nports = XHCI_HCS1_MAXPORTS(hcs1);
    hc->maxslots = XHCI_HCS1_MAXSLOTS(hcs1);
    if (hc->maxslots > XHCI_MAX_SLOTS) hc->maxslots = XHCI_MAX_SLOTS;
    hc->ctx_size = (rd32(hc->mmio, XHCI_CAP_HCCPARAMS1) & XHCI_HCC1_CSZ) ? 64 : 32;
    if (hc->nports == 0) hc->nports = 1;

    /*
     * Every one of these offsets comes from the controller, and we map a fixed
     * window rather than the BAR's real size.  Unchecked, a part that places
     * its runtime or doorbell region past the window -- or a bad read that
     * returns all-ones -- would have us writing doorbells and ERDP updates
     * into whatever happens to follow the mapping.  Bound them and refuse the
     * attach instead, naming the register that was out of range. [X-09]
     */
    /* CAPLENGTH is 8-bit, so it cannot itself leave the window; it only has
     * to be big enough to cover the capability registers we read above. */
    if (caplen < 0x20 ||
        (uint32_t)caplen + XHCI_OP_PORTSC(hc->nports) > XHCI_MMIO_SIZE ||
        rtsoff + XHCI_RT_IR0 + 0x20 > XHCI_MMIO_SIZE ||
        dboff + ((uint32_t)hc->maxslots + 1) * 4 > XHCI_MMIO_SIZE) {
        kprintf("xhci: register map does not fit the %u-byte window "
                "(caplen=0x%x rtsoff=0x%x dboff=0x%x ports=%u)\n",
                XHCI_MMIO_SIZE, caplen, (unsigned)rtsoff, (unsigned)dboff,
                hc->nports);
        xhci_teardown(hc);
        return -1;
    }
    hc->op = hc->mmio + caplen;
    hc->rt = hc->mmio + rtsoff;
    hc->db = hc->mmio + dboff;

    mutex_init(&hc->submit_lock, "xhci_submit");

    /*
     * The BIOS handoff has to come first: it settles who owns the controller,
     * and we must own it before we reset it.
     */
    if (!cmdline_has("nousbhandoff"))
        xhci_take_controller(hc);

    /* Which protocol each port speaks; needed to read PORTSC's speed
     * field correctly.  Static capability data, safe to read before the
     * controller is started. [X-10] */
    xhci_parse_protocols(hc);

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

    /*
     * Opt-in, not opt-out.  On a Lenovo C460 (Lynx Point) the XUSB2PR write
     * wedges the machine dead under a legacy BIOS boot -- the trace stops on
     * the line announcing it and "USB2 ports routed" never prints.  That is
     * with the bare four-access sequence both BSDs use, before the controller
     * and after it, so no sequencing avoids it; the machine survives the same
     * write under UEFI, which points at the firmware's legacy USB emulation
     * servicing it in SMM rather than at anything the driver does.
     *
     * Nothing is lost by leaving the ports alone: they stay with the EHCI,
     * which drives them perfectly well.  The reroute only decides which
     * controller owns a USB2 port, not whether it works.  A wedged machine is
     * a far worse outcome than a USB2 device on the companion, so it takes an
     * explicit "xhciroute" to ask for it.
     */
    if (cmdline_has("xhciroute")) {
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
    hc->hcd.port_gone = xhci_port_gone;
    usb_register_hcd(&hc->hcd);
    hc->initialized = 1;
    xhci_instances++;
    kprintf("xhci: %s: USB 3.x controller at 0x%x, %u ports, %u slots, ctx=%u, "
            "scratch=%u\n",
            hc->name, (unsigned)phys, hc->nports, hc->maxslots, hc->ctx_size,
            (unsigned)hc->nscratch);
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
