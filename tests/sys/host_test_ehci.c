/*
 * host_test_ehci.c — host-side tests for the EHCI driver's decision logic.
 *
 * Compiles sys/drivers/usb/ehci.c into this TU (the nvme.c HOST_TEST
 * pattern) with the operational-register accessors redirected to a scripted
 * fake controller, then drives the poll loops and port operations through
 * the states QEMU cannot produce and the ehci-audit fixes exist for:
 *
 *   - Host System Error mid-poll        [EHCI-INIT-03 / ehci-audit 11]
 *   - async schedule refusing to stop   [ASYNC-04 / ehci-audit 3]
 *   - completion landing during the stop window  [EHCI-03 / ehci-audit 3]
 *   - stuck Port Reset                  [PORT-02 / ehci-audit 8]
 *   - Line-Status validity gating       [PORT-01 / ehci-audit 9]
 *   - W1C change-bit preservation       [ehci-audit 10]
 *   - RL=0 on interrupt queue heads     [ehci-audit 5]
 *
 * plus table tests for the pure calculators (ehci_halt_status,
 * ehci_intr_stride, ehci_endp_char/ehci_endp_cap, ehci_fill_qtd).  These
 * assert against hand-derived spec values, never against a mirror copy of
 * the driver's own arithmetic.
 *
 * Known limitation, on purpose: the overlay park/publish STORE ORDERING of
 * [ehci-audit 1] is a concurrency property a single-threaded fake cannot
 * check; it remains a review property.
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
int64_t get_uptime_ms(void)
{
    /* Every read advances time 1 ms, so bounded waits terminate quickly
     * and deadline loops see monotonic progress. */
    return mock_time_ms++;
}

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

static uintptr_t mock_dma_next = 0x100000;
void *dma_alloc_coherent(size_t size, dma_addr_t *dma_handle)
{
    void *p = calloc(1, (size + 0xFFF) & ~(size_t)0xFFF);
    if (dma_handle)
        *dma_handle = (dma_addr_t)mock_dma_next;
    mock_dma_next += (size + 0xFFF) & ~(size_t)0xFFF;
    return p;
}
void dma_free_coherent(void *cpu_addr, size_t size) { (void)size; free(cpu_addr); }

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
void usb_delay_ms(uint32_t ms) { mock_time_ms += ms; }

/* usb_tt_hub: configurable per test. */
static usb_device_t *mock_tt_hub;
static uint8_t mock_tt_port;
usb_device_t *usb_tt_hub(const usb_device_t *dev, uint8_t *ttport)
{
    (void)dev;
    if (mock_tt_hub && ttport)
        *ttport = mock_tt_port;
    return mock_tt_hub;
}

/* ------------------------------------------------------------------ */
/* Fake operational-register file + scripted behaviors                */
/* ------------------------------------------------------------------ */

#define FAKE_OP_WORDS 64            /* 0x100 bytes: covers PORTSC[0..N] */
static uint32_t fake_regs[FAKE_OP_WORDS];
static struct {
    int refuse_stop;        /* ASS never clears when ASE is cleared */
    int stuck_pr;           /* PR never clears on deassert */
    int hs_device;          /* successful reset sets PED (high-speed) */
    int hse_after_sts_reads;/* inject HSE after N USBSTS reads (0 = never) */
    int complete_on_stop;   /* clear qTD ACTIVE bits when ASE is cleared */
    int sts_reads;
} beh;

struct ehci_qtd;                    /* provided by the ehci.c TU below */
static struct ehci_qtd *fake_qtd_pool;
static int fake_qtd_count;
static void fake_complete_qtds(void);

/* Forward declarations matching the HOST_TEST hooks in ehci.c. */
struct ehci_hc;
uint32_t ehci_test_op_rd(struct ehci_hc *hc, uint32_t reg);
void ehci_test_op_wr(struct ehci_hc *hc, uint32_t reg, uint32_t val);

#include "../../sys/drivers/usb/ehci.c"

static void fake_complete_qtds(void)
{
    for (int i = 0; i < fake_qtd_count; i++)
        fake_qtd_pool[i].token &= ~EHCI_QTD_STATUS_ACTIVE;
}

uint32_t ehci_test_op_rd(struct ehci_hc *hc, uint32_t reg)
{
    (void)hc;
    assert(reg / 4 < FAKE_OP_WORDS);
    if (reg == EHCI_OP_USBSTS) {
        beh.sts_reads++;
        if (beh.hse_after_sts_reads &&
            beh.sts_reads >= beh.hse_after_sts_reads)
            fake_regs[reg / 4] |= EHCI_STS_HSE;
    }
    return fake_regs[reg / 4];
}

void ehci_test_op_wr(struct ehci_hc *hc, uint32_t reg, uint32_t val)
{
    (void)hc;
    assert(reg / 4 < FAKE_OP_WORDS);

    if (reg == EHCI_OP_USBSTS) {
        /* W1C register: writing 1 clears. */
        fake_regs[reg / 4] &= ~(val & (EHCI_STS_HSE | EHCI_STS_IAA));
        return;
    }
    if (reg == EHCI_OP_USBCMD) {
        uint32_t old = fake_regs[reg / 4];
        fake_regs[reg / 4] = val;
        if ((old & EHCI_CMD_ASE) && !(val & EHCI_CMD_ASE)) {
            /* ASE cleared: schedule stops (unless scripted otherwise). */
            if (beh.complete_on_stop)
                fake_complete_qtds();
            if (!beh.refuse_stop)
                fake_regs[EHCI_OP_USBSTS / 4] &= ~EHCI_STS_ASS;
        }
        if (!(old & EHCI_CMD_ASE) && (val & EHCI_CMD_ASE))
            fake_regs[EHCI_OP_USBSTS / 4] |= EHCI_STS_ASS;
        return;
    }
    if (reg >= EHCI_OP_PORTSC && reg < EHCI_OP_PORTSC + 16 * 4) {
        uint32_t old = fake_regs[reg / 4];
        uint32_t stored;
        /* W1C change bits: a written 1 clears the stored bit; a written 0
         * preserves it.  Everything else is plain RW. */
        stored = (val & ~EHCI_PORT_CLEAR) | (old & EHCI_PORT_CLEAR & ~val);
        if ((old & EHCI_PORT_RESET) && !(val & EHCI_PORT_RESET)) {
            /* PR deassert: controller completes the reset... */
            if (beh.stuck_pr) {
                stored |= EHCI_PORT_RESET;          /* ...or refuses to */
            } else if (beh.hs_device) {
                stored |= EHCI_PORT_ENABLE;         /* HS: HC sets PED */
            }
        }
        fake_regs[reg / 4] = stored;
        return;
    }
    fake_regs[reg / 4] = val;
}

/* ------------------------------------------------------------------ */
/* Fixtures                                                           */
/* ------------------------------------------------------------------ */

static ehci_hc_t *make_hc(void)
{
    ehci_hc_t *hc = calloc(1, sizeof(*hc));

    memset(fake_regs, 0, sizeof(fake_regs));
    memset(&beh, 0, sizeof(beh));
    kprintf_calls = 0;
    mock_time_ms = 0;

    hc->qtd = calloc(EHCI_MAX_QTD, sizeof(struct ehci_qtd));
    hc->qtd_dma = 0x200000;
    hc->async_qh = calloc(1, sizeof(struct ehci_qh));
    hc->async_qh_dma = 0x300000;
    hc->intr_qh = calloc(1, sizeof(struct ehci_qh));
    hc->intr_qh_dma = 0x310000;
    hc->periodic = calloc(EHCI_FRAMELIST_ENTRIES, sizeof(uint32_t));
    hc->periodic_dma = 0x320000;
    hc->bounce = calloc(1, EHCI_BOUNCE_SIZE);
    hc->bounce_dma = 0x400000;
    hc->setup_buf = calloc(1, 64);
    hc->setup_dma = 0x500000;
    hc->nports = 4;
    hc->hcd.priv = hc;
    hc->hcd.name = "ehci-fake";
    fake_qtd_pool = hc->qtd;
    fake_qtd_count = EHCI_MAX_QTD;

    /* Controller nominally running with the async schedule up. */
    fake_regs[EHCI_OP_USBCMD / 4] = EHCI_CMD_RUN | EHCI_CMD_ASE | EHCI_CMD_PSE;
    fake_regs[EHCI_OP_USBSTS / 4] = EHCI_STS_ASS | EHCI_STS_PSS;
    return hc;
}

static void free_hc(ehci_hc_t *hc)
{
    free(hc->qtd); free(hc->async_qh); free(hc->intr_qh);
    free(hc->periodic); free(hc->bounce); free(hc->setup_buf); free(hc);
}

static uint32_t portsc(int port) { return fake_regs[(EHCI_OP_PORTSC + (port - 1) * 4) / 4]; }
static void set_portsc(int port, uint32_t v) { fake_regs[(EHCI_OP_PORTSC + (port - 1) * 4) / 4] = v; }

/* ------------------------------------------------------------------ */
/* Table tests: pure calculators                                      */
/* ------------------------------------------------------------------ */

static void test_halt_status(void)
{
    /* Pure STALL: only the Halted bit -> functional stall. */
    assert(ehci_halt_status(EHCI_QTD_STATUS_HALTED) == USB_XFER_STALL);
    /* Any cause bit alongside -> transport error, NOT a stall. */
    assert(ehci_halt_status(EHCI_QTD_STATUS_HALTED |
                            EHCI_QTD_STATUS_XACTERR) == USB_XFER_ERROR);
    assert(ehci_halt_status(EHCI_QTD_STATUS_HALTED |
                            EHCI_QTD_STATUS_BABBLE) == USB_XFER_ERROR);
    assert(ehci_halt_status(EHCI_QTD_STATUS_HALTED |
                            EHCI_QTD_STATUS_BUFERR) == USB_XFER_ERROR);
    assert(ehci_halt_status(EHCI_QTD_STATUS_HALTED |
                            EHCI_QTD_STATUS_MISSED) == USB_XFER_ERROR);
    printf("  halt_status: ok\n");
}

static void test_intr_stride(void)
{
    /* High speed: period = 2^(bInterval-1) microframes, 8 per frame,
     * floored to >= 1 frame.  Hand-derived from EHCI 3.6.2 / USB 2.0 9.6.6:
     *   bi=1 -> 1 uf  -> 0 frames -> floor 1
     *   bi=4 -> 8 uf  -> 1 frame
     *   bi=5 -> 16 uf -> 2 frames
     *   bi=8 -> 128uf -> 16 frames
     *   bi=14 -> 8192uf -> 1024 frames -> clamp 512
     *   bi=20 (out of range) -> clamped exponent -> clamp 512 */
    assert(ehci_intr_stride(1, USB_SPEED_HIGH) == 1);
    assert(ehci_intr_stride(2, USB_SPEED_HIGH) == 1);
    assert(ehci_intr_stride(3, USB_SPEED_HIGH) == 1);
    assert(ehci_intr_stride(4, USB_SPEED_HIGH) == 1);
    assert(ehci_intr_stride(5, USB_SPEED_HIGH) == 2);
    assert(ehci_intr_stride(8, USB_SPEED_HIGH) == 16);
    assert(ehci_intr_stride(14, USB_SPEED_HIGH) == 512);
    assert(ehci_intr_stride(20, USB_SPEED_HIGH) == 512);
    /* Full/low speed: bInterval is frames, directly; 0 defends to 1. */
    assert(ehci_intr_stride(0, USB_SPEED_FULL) == 1);
    assert(ehci_intr_stride(1, USB_SPEED_FULL) == 1);
    assert(ehci_intr_stride(10, USB_SPEED_LOW) == 10);
    assert(ehci_intr_stride(255, USB_SPEED_FULL) == 255);
    printf("  intr_stride: ok\n");
}

static void test_endp_char(void)
{
    usb_device_t dev;
    usb_endpoint_t ep;
    usb_transfer_t xfer;

    memset(&dev, 0, sizeof(dev));
    memset(&ep, 0, sizeof(ep));
    memset(&xfer, 0, sizeof(xfer));
    dev.address = 3;
    dev.speed = USB_SPEED_HIGH;
    ep.address = 0x81;
    ep.type = USB_EP_TYPE_BULK;
    ep.max_packet = 512;
    xfer.dev = &dev;
    xfer.ep = &ep;

    /* HS bulk: RL=4 (NAK throttle allowed on async). */
    uint32_t ec = ehci_endp_char(&xfer, 0);
    assert(((ec >> EHCI_QH_NRL_SHIFT) & 0xF) == 4);
    assert((ec & 0x7F) == 3);                        /* device address */
    assert(((ec >> EHCI_QH_ENDPT_SHIFT) & 0xF) == 1);
    assert(((ec >> EHCI_QH_MPL_SHIFT) & 0x7FF) == 512);
    assert((ec & (3u << 12)) == EHCI_QH_EPS_HIGH);

    /* HS interrupt: 4.9 "Software must use [RL=0] for interrupt
     * endpoints" -- the periodic schedule has no reload machinery. */
    ep.type = USB_EP_TYPE_INTERRUPT;
    ec = ehci_endp_char(&xfer, 0);
    assert(((ec >> EHCI_QH_NRL_SHIFT) & 0xF) == 0);

    /* Non-HS control needs the Control Endpoint Flag (Table 3-19 C). */
    dev.speed = USB_SPEED_LOW;
    ep.type = USB_EP_TYPE_CONTROL;
    ep.max_packet = 8;
    ec = ehci_endp_char(&xfer, 1);
    assert(ec & EHCI_QH_CONTROL_EP);
    assert((ec & (3u << 12)) == EHCI_QH_EPS_LOW);
    /* ...and no NAK throttle off high speed. */
    assert(((ec >> EHCI_QH_NRL_SHIFT) & 0xF) == 0);

    /* MPL is architecturally capped at 0x400 (Table 3-19). */
    dev.speed = USB_SPEED_HIGH;
    ep.type = USB_EP_TYPE_BULK;
    ep.max_packet = 2047;                            /* malformed descriptor */
    ec = ehci_endp_char(&xfer, 0);
    assert(((ec >> EHCI_QH_MPL_SHIFT) & 0x7FF) == 1024);
    printf("  endp_char: ok\n");
}

static void test_endp_cap(void)
{
    usb_device_t dev, hub;
    usb_endpoint_t ep;
    usb_transfer_t xfer;

    memset(&dev, 0, sizeof(dev));
    memset(&hub, 0, sizeof(hub));
    memset(&ep, 0, sizeof(ep));
    memset(&xfer, 0, sizeof(xfer));
    dev.speed = USB_SPEED_HIGH;
    ep.address = 0x81;
    ep.type = USB_EP_TYPE_INTERRUPT;
    xfer.dev = &dev;
    xfer.ep = &ep;
    mock_tt_hub = NULL;

    /* HS interrupt: S-mask nonzero (one poll slot), no C-mask. */
    uint32_t cap = ehci_endp_cap(&xfer, 1);
    assert(((cap >> EHCI_QH_SMASK_SHIFT) & 0xFF) == 0x02);
    assert(((cap >> EHCI_QH_CMASK_SHIFT) & 0xFF) == 0);
    assert(((cap >> EHCI_QH_MULT_SHIFT) & 0x3) == 1);

    /* FS interrupt behind a TT hub: split S/C masks + hub routing.
     * S=uframe1, C=uframes 3,4,5 -- a legal Case-1 layout (4.12.2.1)
     * with completes at S+2..S+4. */
    dev.speed = USB_SPEED_FULL;
    hub.address = 2;
    mock_tt_hub = &hub;
    mock_tt_port = 4;
    cap = ehci_endp_cap(&xfer, 1);
    assert(((cap >> EHCI_QH_SMASK_SHIFT) & 0xFF) == 0x02);
    assert(((cap >> EHCI_QH_CMASK_SHIFT) & 0xFF) == 0x38);
    assert(((cap >> EHCI_QH_HUBA_SHIFT) & 0x7F) == 2);
    assert(((cap >> EHCI_QH_PORT_SHIFT) & 0x7F) == 4);
    mock_tt_hub = NULL;
    printf("  endp_cap: ok\n");
}

static void test_fill_qtd(void)
{
    ehci_hc_t *hc = make_hc();

    /* 6000 bytes from a page-aligned buffer: page 0 pointer carries the
     * base, page 1 the next 4K boundary (4.10.6 / Figure 4-15). */
    ehci_fill_qtd(hc, 0, 0x1234560, 0x7654320, EHCI_QTD_PID_IN, 6000, 1,
                  hc->bounce_dma, 0);
    struct ehci_qtd *q = &hc->qtd[0];
    assert(q->next == 0x1234560);
    assert(q->alt_next == 0x7654320);
    assert(q->buffer[0] == (uint32_t)hc->bounce_dma);
    assert(q->buffer[1] == (uint32_t)hc->bounce_dma + 0x1000);
    assert(q->buffer[2] == (uint32_t)hc->bounce_dma + 0x2000);
    assert(((q->token >> EHCI_QTD_BYTES_SHIFT) & 0x7FFF) == 6000);
    assert(q->token & EHCI_QTD_STATUS_ACTIVE);
    assert(q->token & EHCI_QTD_TOGGLE);
    assert(((q->token >> EHCI_QTD_CERR_SHIFT) & 0x3) == 3);

    /* alt=0 means end-of-chain semantics: T-bit terminator. */
    ehci_fill_qtd(hc, 1, 0, 0, EHCI_QTD_PID_OUT, 0, 0, 0, 1);
    assert(hc->qtd[1].alt_next == EHCI_LINK_TERMINATE);
    assert(hc->qtd[1].next == EHCI_LINK_TERMINATE);
    assert(!(hc->qtd[1].token & EHCI_QTD_TOGGLE));

    /* The control data qTD's alt_next must point at the status qTD --
     * the [ehci-audit 6] 3.5.2-vs-4.10.2 defense.  Reproduce the control
     * path's calls and check the chain wiring. */
    ehci_fill_qtd(hc, 2, 0, 0, EHCI_QTD_PID_IN, 0, 1, 0, 1);   /* status */
    ehci_fill_qtd(hc, 1, ehci_qtd_dma(hc, 2), ehci_qtd_dma(hc, 2),
                  EHCI_QTD_PID_IN, 64, 1, hc->bounce_dma, 0);  /* data */
    assert(hc->qtd[1].next == ehci_qtd_dma(hc, 2));
    assert(hc->qtd[1].alt_next == ehci_qtd_dma(hc, 2));

    free_hc(hc);
    printf("  fill_qtd: ok\n");
}

/* ------------------------------------------------------------------ */
/* Scenario tests: the audit semantics QEMU cannot produce            */
/* ------------------------------------------------------------------ */

static void test_hse_mid_poll(void)
{
    ehci_hc_t *hc = make_hc();

    /* One armed qTD that never completes; HSE appears at the second
     * USBSTS read (the first is consumed by nothing here -- the throttled
     * probe reads once per 1024 spins). */
    hc->qtd[0].token = EHCI_QTD_STATUS_ACTIVE;
    beh.hse_after_sts_reads = 1;

    int r = ehci_run_qh(hc, 0, 1, 0, 5000, 0);
    assert(r == USB_XFER_ERROR);
    assert(hc->hcd.hc_failed == 1);
    /* A halted HC does no DMA: the token must have been neutralized so a
     * stale visit is impossible. */
    assert(!(hc->qtd[0].token & EHCI_QTD_STATUS_ACTIVE));
    /* HSE acknowledged (W1C) so a single fault is not re-reported. */
    assert(!(fake_regs[EHCI_OP_USBSTS / 4] & EHCI_STS_HSE));
    free_hc(hc);
    printf("  hse_mid_poll: ok\n");
}

static void test_refused_stop_no_scribble(void)
{
    ehci_hc_t *hc = make_hc();

    /* Timeout path with a schedule that never acknowledges stopping:
     * the driver must NOT scribble on qTDs/overlay the HC may still be
     * walking, must latch the failure, and must not re-enable ASE
     * against the 4.8 ASE==ASS rule.  (The ARMING writes the overlay --
     * that is its job; the property under test is that the TIMEOUT path
     * leaves everything exactly as armed when the stop fails.) */
    hc->qtd[0].token = EHCI_QTD_STATUS_ACTIVE | (64u << EHCI_QTD_BYTES_SHIFT);
    beh.refuse_stop = 1;

    int r = ehci_run_qh(hc, 0, 1, 0, 10, 0);
    assert(r == USB_XFER_TIMEOUT);
    assert(hc->hcd.hc_failed == 1);
    /* No scribble: token still ACTIVE; overlay still holds the ARMING
     * values (next -> our qTD, token published as 0), not the
     * neutralization values (next=TERMINATE, token HALTED). */
    assert(hc->qtd[0].token & EHCI_QTD_STATUS_ACTIVE);
    assert(hc->async_qh->overlay_next == ehci_qtd_dma(hc, 0));
    assert(hc->async_qh->overlay_token == 0);
    /* ASE must not have been re-set while ASS stayed high: the stored
     * USBCMD keeps ASE cleared. */
    assert(!(fake_regs[EHCI_OP_USBCMD / 4] & EHCI_CMD_ASE));
    free_hc(hc);
    printf("  refused_stop_no_scribble: ok\n");
}

static void test_late_completion_rescued(void)
{
    ehci_hc_t *hc = make_hc();

    /* The transfer completes in the window between the last token sample
     * and the verified stop -- the fake clears ACTIVE when ASE is cleared.
     * The old code returned TIMEOUT and threw the completion away. */
    hc->qtd[0].token = EHCI_QTD_STATUS_ACTIVE;   /* zero residue when done */
    beh.complete_on_stop = 1;

    int r = ehci_run_qh(hc, 0, 1, 0, 10, 0);
    assert(r == USB_XFER_OK);
    assert(hc->hcd.hc_failed == 0);
    /* Schedule restarted for the next transfer. */
    assert(fake_regs[EHCI_OP_USBCMD / 4] & EHCI_CMD_ASE);
    assert(fake_regs[EHCI_OP_USBSTS / 4] & EHCI_STS_ASS);
    free_hc(hc);
    printf("  late_completion_rescued: ok\n");
}

static void test_stuck_pr_no_disown(void)
{
    ehci_hc_t *hc = make_hc();

    set_portsc(2, EHCI_PORT_CONNECT | EHCI_PORT_POWER);
    beh.stuck_pr = 1;

    int r = ehci_port_reset(&hc->hcd, 2);
    assert(r == -1);
    /* The port must NOT have been handed to a companion with PR live. */
    assert(!(portsc(2) & EHCI_PORT_OWNER));
    assert(portsc(2) & EHCI_PORT_RESET);   /* the fault state, observable */
    free_hc(hc);
    printf("  stuck_pr_no_disown: ok\n");
}

static void test_kstate_gating(void)
{
    ehci_hc_t *hc = make_hc();

    /* Disabled port in K state: low-speed device, companion handoff. */
    set_portsc(1, EHCI_PORT_CONNECT | EHCI_PORT_POWER | EHCI_PORT_LS_KSTATE);
    int r = ehci_port_reset(&hc->hcd, 1);
    assert(r == -1);
    assert(portsc(1) & EHCI_PORT_OWNER);

    /* ENABLED port with a garbage K reading: Line Status is undefined
     * while PED=1 (Table 2-16), so the handoff must NOT fire; the reset
     * proceeds and re-enables the high-speed device. */
    beh.hs_device = 1;
    set_portsc(2, EHCI_PORT_CONNECT | EHCI_PORT_POWER | EHCI_PORT_ENABLE |
                  EHCI_PORT_LS_KSTATE);
    r = ehci_port_reset(&hc->hcd, 2);
    assert(r == 0);
    assert(!(portsc(2) & EHCI_PORT_OWNER));
    assert(portsc(2) & EHCI_PORT_ENABLE);
    free_hc(hc);
    printf("  kstate_gating: ok\n");
}

static void test_w1c_preservation(void)
{
    ehci_hc_t *hc = make_hc();

    /* Pending change bits must survive every PORTSC RMW the driver does:
     * an unrelated write acknowledging them is a lost hot-plug event. */
    beh.hs_device = 1;
    set_portsc(3, EHCI_PORT_CONNECT | EHCI_PORT_POWER |
                  EHCI_PORT_CONNECT_CH | EHCI_PORT_ENABLE_CH |
                  EHCI_PORT_OC_CH);
    int r = ehci_port_reset(&hc->hcd, 3);
    assert(r == 0);
    assert(portsc(3) & EHCI_PORT_CONNECT_CH);
    assert(portsc(3) & EHCI_PORT_ENABLE_CH);
    assert(portsc(3) & EHCI_PORT_OC_CH);

    free_hc(hc);
    printf("  w1c_preservation: ok\n");
}

int main(void)
{
    printf("host_test_ehci:\n");
    test_halt_status();
    test_intr_stride();
    test_endp_char();
    test_endp_cap();
    test_fill_qtd();
    test_hse_mid_poll();
    test_refused_stop_no_scribble();
    test_late_completion_rescued();
    test_stuck_pr_no_disown();
    test_kstate_gating();
    test_w1c_preservation();
    printf("host_test_ehci: all tests passed\n");
    return 0;
}
