/*
 * host_test_uhci.c — host-side tests for the UHCI driver's decision logic.
 *
 * Same shape as host_test_ehci.c: compiles uhci.c into this TU with the
 * I/O-port funnel (uhci_readw/writew/writel) redirected to a scripted fake
 * controller.  Covers the RF-4 parity fixes:
 *
 *   - TD status classification, cause bits before STALLED
 *   - PORTSC W1C preservation in uhci_port_enable
 *   - honest uhci_port_reset (a port that never enables reports failure)
 *   - dead-controller detection in the poll loop (HSE/HCPE/HCH)
 *   - zero-length bulk transfers accepted (BOT terminating ZLP)
 *   - no toggle advance on STALL (usb_clear_halt owns that reset)
 *
 * Multi-TD chain walks convert physical link pointers with a fixed
 * +KERN_BASE offset the host cannot honor, so every scenario here uses
 * single-TD chains (link = terminate); the chain-walk arithmetic itself is
 * kernel-only territory.
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
/* Mock kernel environment                                            */
/* ------------------------------------------------------------------ */

static int64_t mock_time_ms;
int64_t get_uptime_ms(void) { return mock_time_ms++; }

static int kprintf_calls;
static char last_kprintf[256];
int kprintf(const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    vsnprintf(last_kprintf, sizeof(last_kprintf), fmt, ap);
    __builtin_va_end(ap);
    kprintf_calls++;
    return 0;
}
void kprint(const char *s) { (void)s; kprintf_calls++; }

void mutex_init(mutex_t *m, const char *name) { (void)m; (void)name; }
void mutex_lock(mutex_t *m) { (void)m; }
void mutex_unlock(mutex_t *m) { (void)m; }
void *kzalloc(size_t size) { return calloc(1, size); }
void kfree(void *ptr, size_t size) { (void)size; free(ptr); }

uint32_t intr_disable(void) { return 0; }
void intr_enable(void) { }
void intr_restore(uint32_t flags) { (void)flags; }
void sched_yield(void) { }

void *dma_alloc_coherent(size_t size, dma_addr_t *dma_handle)
{
    void *p = calloc(1, (size + 0xFFF) & ~(size_t)0xFFF);
    static uintptr_t next = 0x100000;
    if (dma_handle) { *dma_handle = (dma_addr_t)next; next += 0x10000; }
    return p;
}
void dma_free_coherent(void *cpu_addr, size_t size) { (void)size; free(cpu_addr); }
dma_addr_t dma_map_single(void *cpu_addr, size_t size, enum dma_data_direction dir)
{ (void)size; (void)dir; return (dma_addr_t)(uintptr_t)cpu_addr; }
void dma_unmap_single(dma_addr_t dma_addr, size_t size, enum dma_data_direction dir)
{ (void)dma_addr; (void)size; (void)dir; }

int cmdline_has(const char *key) { (void)key; return 0; }
int pci_present(void) { return 0; }
pci_device_t *pci_find_device_by_kdev(struct device *dev) { (void)dev; return NULL; }
int pci_bar_type(pci_device_t *dev, int bar) { (void)dev; (void)bar; return 0; }
uint32_t pci_read_config32(uint8_t b, uint8_t s, uint8_t f, uint16_t o)
{ (void)b; (void)s; (void)f; (void)o; return 0; }
uint16_t pci_read_config16(uint8_t b, uint8_t s, uint8_t f, uint16_t o)
{ (void)b; (void)s; (void)f; (void)o; return 0; }
void pci_write_config16(uint8_t b, uint8_t s, uint8_t f, uint16_t o, uint16_t v)
{ (void)b; (void)s; (void)f; (void)o; (void)v; }
void pci_write_config32(uint8_t b, uint8_t s, uint8_t f, uint16_t o, uint32_t v)
{ (void)b; (void)s; (void)f; (void)o; (void)v; }
int pci_get_irq(pci_device_t *dev) { (void)dev; return 11; }
int driver_register(struct driver *drv, struct bus_type *bus)
{ (void)drv; (void)bus; return 0; }
struct bus_type pci_bus_type;
int usb_register_hcd(usb_hcd_t *hcd) { (void)hcd; return 0; }
usb_hcd_t *usb_hcd_by_kdev(struct device *dev) { (void)dev; return NULL; }
void usb_delay_ms(uint32_t ms) { mock_time_ms += ms; }

/* ------------------------------------------------------------------ */
/* Fake I/O-port register file + scripted behaviors                   */
/* ------------------------------------------------------------------ */

#define FAKE_REGS 32                    /* byte offsets 0x00..0x3F, u16 each */
static uint16_t fake_regs[FAKE_REGS];
static struct {
    int pe_stuck_off;    /* PE writes never stick (broken port) */
    int halt_on_sts;     /* USBSTS reports HCH from the Nth read on */
    int complete_on_sts; /* clear TD ACTIVE when USBSTS is read (poll loop) */
    int stall_on_sts;    /* retire TD with STALLED when USBSTS is read */
    int sts_reads;
} beh;

struct uhci_td;
static struct uhci_td *fake_td_watch;
static void fake_retire_td(int stall);

struct uhci_hc;
uint16_t uhci_test_readw(struct uhci_hc *hc, uint16_t reg);
void uhci_test_writew(struct uhci_hc *hc, uint16_t reg, uint16_t val);
void uhci_test_writel(struct uhci_hc *hc, uint16_t reg, uint32_t val);

#include "../../sys/drivers/usb/uhci.c"

static void fake_retire_td(int stall)
{
    if (!fake_td_watch)
        return;
    fake_td_watch->ctrl_status &= ~UHCI_TD_CTRL_ACTIVE;
    if (stall)
        fake_td_watch->ctrl_status |= UHCI_TD_CTRL_STALLED;
}

uint16_t uhci_test_readw(struct uhci_hc *hc, uint16_t reg)
{
    (void)hc;
    assert(reg / 2 < FAKE_REGS);
    if (reg == UHCI_USBSTS) {
        beh.sts_reads++;
        if (beh.halt_on_sts && beh.sts_reads >= beh.halt_on_sts)
            fake_regs[reg / 2] |= UHCI_STS_HCH;
        if (beh.complete_on_sts)
            fake_retire_td(0);
        if (beh.stall_on_sts)
            fake_retire_td(1);
    }
    return fake_regs[reg / 2];
}

void uhci_test_writew(struct uhci_hc *hc, uint16_t reg, uint16_t val)
{
    (void)hc;
    assert(reg / 2 < FAKE_REGS);
    if (reg == UHCI_USBSTS) {
        /* Status register is W1C. */
        fake_regs[reg / 2] &= ~val;
        return;
    }
    if (reg == UHCI_PORTSC1 || reg == UHCI_PORTSC2) {
        uint16_t old = fake_regs[reg / 2];
        /* W1C: a written 1 clears CSC/PEC; a written 0 preserves. */
        uint16_t stored = (val & ~UHCI_PORTSC_CLEAR) |
                          (old & UHCI_PORTSC_CLEAR & ~val);
        if (beh.pe_stuck_off)
            stored &= ~UHCI_PORTSC_PE;
        fake_regs[reg / 2] = stored;
        return;
    }
    fake_regs[reg / 2] = val;
}

void uhci_test_writel(struct uhci_hc *hc, uint16_t reg, uint32_t val)
{
    (void)hc;
    assert(reg / 2 + 1 < FAKE_REGS);
    fake_regs[reg / 2] = (uint16_t)val;
    fake_regs[reg / 2 + 1] = (uint16_t)(val >> 16);
}

/* ------------------------------------------------------------------ */
/* Fixtures                                                           */
/* ------------------------------------------------------------------ */

static uhci_hc_t *make_hc(void)
{
    uhci_hc_t *hc = calloc(1, sizeof(*hc));

    memset(fake_regs, 0, sizeof(fake_regs));
    memset(&beh, 0, sizeof(beh));
    fake_td_watch = NULL;
    kprintf_calls = 0;
    mock_time_ms = 0;

    hc->td_pool = calloc(UHCI_MAX_TDS, sizeof(struct uhci_td));
    hc->td_pool_dma = 0x200000;
    hc->qh_pool = calloc(UHCI_MAX_QHS, sizeof(struct uhci_qh));
    hc->qh_pool_dma = 0x300000;
    hc->async_qh = calloc(1, sizeof(struct uhci_qh));
    hc->async_qh_dma = 0x310000;
    hc->setup_buf = calloc(1, 64);
    hc->setup_buf_dma = 0x320000;
    hc->hcd.priv = hc;
    hc->hcd.name = "uhci-fake";
    return hc;
}

static void free_hc(uhci_hc_t *hc)
{
    free(hc->td_pool); free(hc->qh_pool); free(hc->async_qh);
    free(hc->setup_buf); free(hc);
}

static uint16_t portsc1(void) { return fake_regs[UHCI_PORTSC1 / 2]; }

/* ------------------------------------------------------------------ */
/* Tests                                                              */
/* ------------------------------------------------------------------ */

static void test_td_status(void)
{
    /* Pure STALL handshake: only STALLED set. */
    assert(uhci_td_status(UHCI_TD_CTRL_STALLED) == USB_XFER_STALL);
    /* Error-counter exhaustion sets STALLED *and* the cause bit; the cause
     * must win or transport errors masquerade as functional stalls. */
    assert(uhci_td_status(UHCI_TD_CTRL_STALLED |
                          UHCI_TD_CTRL_CRCTMO) == USB_XFER_ERROR);
    assert(uhci_td_status(UHCI_TD_CTRL_STALLED |
                          UHCI_TD_CTRL_BABBLE) == USB_XFER_ERROR);
    assert(uhci_td_status(UHCI_TD_CTRL_STALLED |
                          UHCI_TD_CTRL_DBUFERR) == USB_XFER_ERROR);
    assert(uhci_td_status(UHCI_TD_CTRL_STALLED |
                          UHCI_TD_CTRL_BITSTUFF) == USB_XFER_ERROR);
    assert(uhci_td_status(0) == USB_XFER_OK);
    printf("  td_status: ok\n");
}

static void test_port_enable_w1c(void)
{
    uhci_hc_t *hc = make_hc();

    /* Pending connect/enable changes must survive an unrelated enable
     * write -- writing them back as 1s acknowledges (= loses) them. */
    fake_regs[UHCI_PORTSC1 / 2] = UHCI_PORTSC_CCS | UHCI_PORTSC_CSC |
                                  UHCI_PORTSC_PEC;
    uhci_port_enable(&hc->hcd, 1, 1);
    assert(portsc1() & UHCI_PORTSC_CSC);
    assert(portsc1() & UHCI_PORTSC_PEC);
    assert(portsc1() & UHCI_PORTSC_PE);
    uhci_port_enable(&hc->hcd, 1, 0);
    assert(portsc1() & UHCI_PORTSC_CSC);
    assert(!(portsc1() & UHCI_PORTSC_PE));
    free_hc(hc);
    printf("  port_enable_w1c: ok\n");
}

static void test_port_reset_honest(void)
{
    uhci_hc_t *hc = make_hc();

    /* A healthy port: PE write sticks, reset reports success. */
    fake_regs[UHCI_PORTSC1 / 2] = UHCI_PORTSC_CCS;
    assert(uhci_port_reset(&hc->hcd, 1) == 0);
    assert(portsc1() & UHCI_PORTSC_PE);

    /* A port whose enable never sticks: the old code returned 0 anyway,
     * so the core's enum_fail parking never engaged and the port was
     * reset-probed forever. */
    memset(fake_regs, 0, sizeof(fake_regs));
    fake_regs[UHCI_PORTSC1 / 2] = UHCI_PORTSC_CCS;
    beh.pe_stuck_off = 1;
    assert(uhci_port_reset(&hc->hcd, 1) == -1);
    free_hc(hc);
    printf("  port_reset_honest: ok\n");
}

static void test_dead_hc_poll(void)
{
    uhci_hc_t *hc = make_hc();
    struct uhci_td td;

    memset(&td, 0, sizeof(td));
    td.ctrl_status = UHCI_TD_CTRL_ACTIVE;
    td.link = UHCI_TD_LINK_T;
    beh.halt_on_sts = 1;

    /* The TD never retires; the controller reports itself halted.  The
     * poll must detect it (throttled USBSTS probe), latch the core
     * fail-fast flag, and return ERROR instead of burning the timeout. */
    int r = uhci_poll_td(hc, &td, 60000, NULL);
    assert(r == USB_XFER_ERROR);
    assert(hc->hcd.hc_failed == 1);
    free_hc(hc);
    printf("  dead_hc_poll: ok\n");
}

static void test_bulk_zlp_accepted(void)
{
    uhci_hc_t *hc = make_hc();
    usb_device_t dev;
    usb_endpoint_t ep;
    usb_transfer_t xfer;

    memset(&dev, 0, sizeof(dev));
    memset(&ep, 0, sizeof(ep));
    memset(&xfer, 0, sizeof(xfer));
    dev.address = 2;
    dev.speed = USB_SPEED_FULL;
    ep.address = 0x02;                     /* OUT */
    ep.max_packet = 64;
    ep.toggle = 0;
    xfer.dev = &dev;
    xfer.ep = &ep;
    xfer.data = NULL;
    xfer.length = 0;                       /* the BOT terminating ZLP */
    xfer.timeout_ms = 5000;

    /* Retire the TD as soon as the poll's dead-check reads USBSTS. */
    beh.complete_on_sts = 1;
    fake_td_watch = hc->td_pool;           /* first allocated TD */

    int r = uhci_bulk_transfer(hc, &xfer);
    assert(r == USB_XFER_OK);
    /* The ZLP is a real transaction: it consumes exactly one toggle. */
    assert(ep.toggle == 1);
    free_hc(hc);
    printf("  bulk_zlp_accepted: ok\n");
}

static void test_no_toggle_advance_on_stall(void)
{
    uhci_hc_t *hc = make_hc();
    usb_device_t dev;
    usb_endpoint_t ep;
    usb_transfer_t xfer;
    uint8_t buf[8] = {0};

    memset(&dev, 0, sizeof(dev));
    memset(&ep, 0, sizeof(ep));
    memset(&xfer, 0, sizeof(xfer));
    dev.address = 2;
    dev.speed = USB_SPEED_FULL;
    ep.address = 0x82;                     /* IN */
    ep.max_packet = 64;
    ep.toggle = 1;
    xfer.dev = &dev;
    xfer.ep = &ep;
    xfer.data = buf;
    xfer.length = sizeof(buf);
    xfer.timeout_ms = 5000;

    beh.stall_on_sts = 1;
    fake_td_watch = hc->td_pool;

    int r = uhci_bulk_transfer(hc, &xfer);
    assert(r == USB_XFER_STALL);
    /* usb_clear_halt resets BOTH sides' toggles; advancing ours on a
     * STALL desynced them.  The toggle must be untouched. */
    assert(ep.toggle == 1);
    free_hc(hc);
    printf("  no_toggle_advance_on_stall: ok\n");
}

int main(void)
{
    printf("host_test_uhci:\n");
    test_td_status();
    test_port_enable_w1c();
    test_port_reset_honest();
    test_dead_hc_poll();
    test_bulk_zlp_accepted();
    test_no_toggle_advance_on_stall();
    printf("host_test_uhci: all tests passed\n");
    return 0;
}
