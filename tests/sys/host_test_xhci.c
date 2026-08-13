/*
 * host_test_xhci.c — host-side tests for the xHCI driver's pure core.
 *
 * Same shape as host_test_ehci.c/host_test_uhci.c: compiles xhci.c into
 * this TU.  No register hook is needed -- rd32/wr32 are plain memory
 * accessors, so pointing hc->op/rt/db at byte arrays is a complete fake.
 *
 * Scope, deliberately narrow (the tier-3 review priced out a full fake-xHC
 * device model as larger than the driver's testable core):
 *
 *   - xhci_xfer_status: the RF-1a completion-code classifier
 *   - xhci_ring_push: producer cycle-bit arithmetic across the Link-TRB
 *     wrap (the missed-cycle-flip failure mode desynchronizes every later
 *     TRB's ownership)
 *   - xhci_ring_make_room: No-Op padding so a TD never spans the link
 *   - xhci_wait_event: consumer cycle matching, dequeue wrap + cycle flip,
 *     ERDP/EHB write-back, and the no-event timeout
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <drivers/usb/usb.h>
#include <kern/device.h>
#include <kern/pci.h>
#include <sys/dma.h>

/* ------------------------------------------------------------------ */
/* Mock kernel environment (same family as the EHCI/UHCI suites)      */
/* ------------------------------------------------------------------ */

static int64_t mock_time_ms;
int64_t get_uptime_ms(void) { return mock_time_ms++; }

static int kprintf_calls;
int kprintf(const char *fmt, ...) { (void)fmt; kprintf_calls++; return 0; }
void kprint(const char *s) { (void)s; kprintf_calls++; }

void mutex_init(mutex_t *m, const char *name) { (void)m; (void)name; }
void mutex_lock(mutex_t *m) { (void)m; }
void mutex_unlock(mutex_t *m) { (void)m; }
void *kzalloc(size_t size) { return calloc(1, size); }
void kfree(void *ptr, size_t size) { (void)size; free(ptr); }
void usb_delay_ms(uint32_t ms) { mock_time_ms += ms; }

/*
 * DMA registry: xhci link TRBs store ring->dma and the tests compare TRB
 * slot addresses against ring->dma arithmetic, so hand out stable fake
 * physical addresses and remember the mapping.
 */
void *dma_alloc_coherent(size_t size, dma_addr_t *dma_handle)
{
    static uintptr_t next = 0x100000;
    void *p = calloc(1, (size + 0xFFF) & ~(size_t)0xFFF);
    if (dma_handle) { *dma_handle = (dma_addr_t)next; next += 0x10000; }
    return p;
}
void dma_free_coherent(void *cpu_addr, size_t size) { (void)size; free(cpu_addr); }
dma_addr_t dma_map_single(void *cpu_addr, size_t size, enum dma_data_direction dir)
{ (void)size; (void)dir; return (dma_addr_t)(uintptr_t)cpu_addr; }
void dma_unmap_single(dma_addr_t dma_addr, size_t size, enum dma_data_direction dir)
{ (void)dma_addr; (void)size; (void)dir; }

void *ioremap(resource_size_t phys_addr, size_t size)
{ (void)phys_addr; (void)size; return NULL; }
void iounmap(void *addr) { (void)addr; }

int cmdline_has(const char *key) { (void)key; return 0; }
int pci_present(void) { return 0; }
pci_device_t *pci_find_device_by_kdev(struct device *dev) { (void)dev; return NULL; }
int pci_bar_type(pci_device_t *dev, int bar) { (void)dev; (void)bar; return 0; }
uint32_t pci_read_config32(uint8_t b, uint8_t s, uint8_t f, uint16_t o)
{ (void)b; (void)s; (void)f; (void)o; return 0; }
uint16_t pci_read_config16(uint8_t b, uint8_t s, uint8_t f, uint16_t o)
{ (void)b; (void)s; (void)f; (void)o; return 0; }
uint8_t pci_read_config8(uint8_t b, uint8_t s, uint8_t f, uint16_t o)
{ (void)b; (void)s; (void)f; (void)o; return 0; }
void pci_write_config32(uint8_t b, uint8_t s, uint8_t f, uint16_t o, uint32_t v)
{ (void)b; (void)s; (void)f; (void)o; (void)v; }
void pci_write_config16(uint8_t b, uint8_t s, uint8_t f, uint16_t o, uint16_t v)
{ (void)b; (void)s; (void)f; (void)o; (void)v; }
void pci_write_config8(uint8_t b, uint8_t s, uint8_t f, uint16_t o, uint8_t v)
{ (void)b; (void)s; (void)f; (void)o; (void)v; }
int pci_get_irq(pci_device_t *dev) { (void)dev; return 11; }
int pci_relocate_bar32(pci_device_t *dev, int bar) { (void)dev; (void)bar; return -1; }
int driver_register(struct driver *drv, struct bus_type *bus)
{ (void)drv; (void)bus; return 0; }
struct bus_type pci_bus_type;
int usb_register_hcd(usb_hcd_t *hcd) { (void)hcd; return 0; }
usb_hcd_t *usb_hcd_by_kdev(struct device *dev) { (void)dev; return NULL; }
usb_device_t *usb_tt_hub(const usb_device_t *dev, uint8_t *ttport)
{ (void)dev; (void)ttport; return NULL; }
uint8_t usb_root_port(const usb_device_t *dev) { (void)dev; return 1; }
uint32_t usb_route_string(const usb_device_t *dev) { (void)dev; return 0; }
size_t pci_bar_size(pci_device_t *dev, int bar) { (void)dev; (void)bar; return 0x4000; }

#include "../../sys/drivers/usb/xhci.c"

/* ------------------------------------------------------------------ */
/* Tests                                                              */
/* ------------------------------------------------------------------ */

static void test_xfer_status(void)
{
    /* cc==0 means no event arrived at all. */
    assert(xhci_xfer_status(0) == USB_XFER_TIMEOUT);
    /* Only a real STALL handshake is a stall. */
    assert(xhci_xfer_status(XHCI_CC_STALL) == USB_XFER_STALL);
    /* Transport/controller failures must NOT report as stalls: callers
     * pick clear-halt vs reset recovery, and HID latches ctl_poll_refused
     * on STALL. */
    assert(xhci_xfer_status(XHCI_CC_BABBLE) == USB_XFER_ERROR);
    assert(xhci_xfer_status(XHCI_CC_USB_TX_ERROR) == USB_XFER_ERROR);
    assert(xhci_xfer_status(XHCI_CC_TRB_ERROR) == USB_XFER_ERROR);
    assert(xhci_xfer_status(XHCI_CC_CONTEXT_STATE) == USB_XFER_ERROR);
    assert(xhci_xfer_status(XHCI_CC_SHORT_PACKET) == USB_XFER_ERROR);
    printf("  xfer_status: ok\n");
}

static void test_ring_push_cycle_wrap(void)
{
    struct xhci_ring r;

    assert(xhci_ring_alloc(&r) == 0);
    assert(r.cycle == 1 && r.enq == 0);
    /* The link TRB is pre-planted with TC and cycle 0 (not yet valid). */
    assert(XHCI_TRB_GET_TYPE(r.trb[XHCI_RING_TRBS - 1].control) == TRB_LINK);
    assert(!(r.trb[XHCI_RING_TRBS - 1].control & XHCI_TRB_CYCLE));

    /* Fill the first lap: slots 0..SIZE-2, all with producer cycle 1. */
    for (int i = 0; i < XHCI_RING_TRBS - 1; i++) {
        uint64_t phys = xhci_ring_push(&r, 0x1000 + i, 0,
                                       XHCI_TRB_TYPE(TRB_NOOP_XFER));
        assert(phys == r.dma + (dma_addr_t)i * sizeof(struct xhci_trb));
        assert(r.trb[i].control & XHCI_TRB_CYCLE);
    }
    /* Hitting the link TRB hands it to the controller (cycle 1), wraps
     * the enqueue and FLIPS the producer cycle -- miss either and every
     * later TRB's ownership bit is wrong. */
    assert(r.enq == 0);
    assert(r.cycle == 0);
    assert(r.trb[XHCI_RING_TRBS - 1].control & XHCI_TRB_CYCLE);

    /* Second lap runs at cycle 0. */
    xhci_ring_push(&r, 0x2000, 0, XHCI_TRB_TYPE(TRB_NOOP_XFER));
    assert(!(r.trb[0].control & XHCI_TRB_CYCLE));
    dma_free_coherent((void *)r.trb, XHCI_RING_TRBS * sizeof(struct xhci_trb));
    printf("  ring_push_cycle_wrap: ok\n");
}

static void test_ring_make_room(void)
{
    struct xhci_ring r;

    assert(xhci_ring_alloc(&r) == 0);
    /* Park the enqueue three slots shy of the link, then ask for a 5-TRB
     * TD: make_room must pad with No-Ops to the wrap so the TD never
     * spans the link (the link carries no Chain bit). */
    for (int i = 0; i < XHCI_RING_TRBS - 4; i++)
        xhci_ring_push(&r, 0, 0, XHCI_TRB_TYPE(TRB_NOOP_XFER));
    assert(r.enq == XHCI_RING_TRBS - 4);
    xhci_ring_make_room(&r, 5);
    assert(r.enq == 0);
    assert(r.cycle == 0);          /* the padding crossed the wrap */
    /* The pad TRBs are real No-Ops the controller retires silently. */
    assert(XHCI_TRB_GET_TYPE(r.trb[XHCI_RING_TRBS - 2].control)
           == TRB_NOOP_XFER);
    dma_free_coherent((void *)r.trb, XHCI_RING_TRBS * sizeof(struct xhci_trb));
    printf("  ring_make_room: ok\n");
}

static void test_wait_event(void)
{
    xhci_hc_t *hc = calloc(1, sizeof(*hc));
    static uint8_t fake_rt[0x100];

    hc->event_ring = calloc(XHCI_RING_TRBS, sizeof(struct xhci_trb));
    hc->event_ring_dma = 0x500000;
    hc->event_deq = 0;
    hc->event_cycle = 1;
    hc->rt = fake_rt;

    /* No event: budget expires, returns 0 ("no event arrived"). */
    mock_time_ms = 0;
    assert(xhci_wait_event(hc, NULL, NULL, NULL, 5) == 0);

    /* Post one event with the matching cycle: consumed, cc extracted,
     * dequeue advanced, ERDP written back with EHB. */
    volatile struct xhci_trb *e = &hc->event_ring[0];
    e->param = 0xDEADBEEF;
    e->status = (uint32_t)XHCI_CC_SUCCESS << 24;
    e->control = XHCI_TRB_CYCLE;   /* cycle 1 = producer's first lap */
    uint64_t param = 0;
    assert(xhci_wait_event(hc, &param, NULL, NULL, 5) == XHCI_CC_SUCCESS);
    assert(param == 0xDEADBEEF);
    assert(hc->event_deq == 1);
    uint64_t erdp = (uint64_t)*(uint32_t *)(fake_rt + XHCI_RT_IR0 + XHCI_IR_ERDP)
                  | ((uint64_t)*(uint32_t *)(fake_rt + XHCI_RT_IR0 + XHCI_IR_ERDP + 4) << 32);
    assert(erdp == (hc->event_ring_dma + sizeof(struct xhci_trb)) + XHCI_ERDP_EHB);

    /* A stale event (wrong cycle) must NOT be consumed. */
    volatile struct xhci_trb *s = &hc->event_ring[1];
    s->status = (uint32_t)XHCI_CC_STALL << 24;
    s->control = 0;                /* cycle 0 against consumer cycle 1 */
    assert(xhci_wait_event(hc, NULL, NULL, NULL, 5) == 0);
    assert(hc->event_deq == 1);

    /* Consumer wrap: consume to the end of the ring, then the consumer
     * cycle flips and lap-2 events (cycle 0) are the live ones. */
    for (uint32_t i = 1; i < XHCI_RING_TRBS; i++) {
        volatile struct xhci_trb *t = &hc->event_ring[i];
        t->status = (uint32_t)XHCI_CC_SUCCESS << 24;
        t->control = XHCI_TRB_CYCLE;
        assert(xhci_wait_event(hc, NULL, NULL, NULL, 5) == XHCI_CC_SUCCESS);
    }
    assert(hc->event_deq == 0);
    assert(hc->event_cycle == 0);
    volatile struct xhci_trb *w = &hc->event_ring[0];
    w->status = (uint32_t)XHCI_CC_SHORT_PACKET << 24;
    w->control = 0;                /* lap-2 producer writes cycle 0 */
    assert(xhci_wait_event(hc, NULL, NULL, NULL, 5) == XHCI_CC_SHORT_PACKET);
    free((void *)hc->event_ring);
    free(hc);
    printf("  wait_event: ok\n");
}

int main(void)
{
    printf("host_test_xhci:\n");
    test_xfer_status();
    test_ring_push_cycle_wrap();
    test_ring_make_room();
    test_wait_event();
    printf("host_test_xhci: all tests passed\n");
    return 0;
}
