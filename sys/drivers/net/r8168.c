/*
 * r8168.c — Realtek RTL8111/8168/8411 PCIe Gigabit Ethernet driver.
 *
 * Despite the name this shares almost nothing with rtl8139.c.  The 8139 is a
 * single circular receive BUFFER with a 4-byte packet header; the 8168 is a
 * descriptor-ring design (the "C+" interface): two rings of 16-byte
 * descriptors, MMIO registers, and an OWN bit handing each descriptor between
 * driver and NIC.  The register offsets that do coincide (IDR0, CR, IMR/ISR,
 * TCR/RCR) mean different things often enough that sharing code would be a
 * trap rather than a saving.
 *
 * Found on essentially every consumer Realtek-equipped machine of the last
 * fifteen years, including the Lenovo C460 (10EC:8168 at 05:00.0) this was
 * written for.
 *
 * TESTING STATUS: compiled and boot-tested, but NOT verified against real
 * hardware, because qemu does not emulate this part -- it offers rtl8139 and
 * nothing else from Realtek.  Everything here is written from the datasheet
 * register map and the ring protocol.  Treat the first run on real silicon as
 * the actual test; the diagnostics below exist for exactly that.
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <arch/i386/intr.h>
#include <arch/i386/pmm.h>
#include <kern/console.h>
#include <kern/driver.h>
#include <kern/pci.h>
#include <sys/irq.h>
#include <sys/lock.h>
#include <sys/netdev.h>
#include <vm/vm_kmem.h>

#define R8168_VENDOR        0x10EC

/* Register offsets (MMIO). */
#define R_IDR0              0x00   /* MAC, 6 bytes */
#define R_MAR0              0x08   /* multicast filter, 8 bytes */
#define R_TNPDS             0x20   /* TX normal-priority desc base, 64-bit */
#define R_CR                0x37   /* command */
#define R_TPPOLL            0x38   /* transmit poll */
#define R_IMR               0x3C   /* interrupt mask, 16-bit */
#define R_ISR               0x3E   /* interrupt status, 16-bit */
#define R_TCR               0x40   /* transmit config, 32-bit */
#define R_RCR               0x44   /* receive config, 32-bit */
#define R_CFG9346           0x50   /* register-write lock */
#define R_MISC              0xF0   /* misc control, 32-bit (8168G and later) */
#define R_PHYSTATUS         0x6C
#define R_RMS               0xDA   /* rx max packet size, 16-bit */
#define R_CPCR              0xE0   /* C+ command, 16-bit */
#define R_RDSAR             0xE4   /* RX desc base, 64-bit */
#define R_ETHRESH           0xEC   /* early TX threshold, 8-bit */

/* CR bits. */
#define CR_TE               0x04
#define CR_RE               0x08
#define CR_RST              0x10

/* TPPoll bits. */
#define TPPOLL_NPQ          0x40   /* kick the normal-priority queue */

/* CFG9346. */
#define CFG9346_UNLOCK      0xC0
#define CFG9346_LOCK        0x00

/* ISR / IMR bits. */
#define INT_ROK             0x0001
#define INT_RER             0x0002
#define INT_TOK             0x0004
#define INT_TER             0x0008
#define INT_RDU             0x0010   /* rx descriptor unavailable */
#define INT_LINKCHG         0x0020
#define INT_FOVW            0x0040   /* rx fifo overflow */
#define INT_TDU             0x0080
#define INT_TIMEOUT         0x4000
#define INT_SERR            0x8000   /* PCI system error */

/* RCR bits. */
#define RCR_AAP             0x00000001   /* accept all (promiscuous) */
#define RCR_APM             0x00000002   /* accept physical match */
#define RCR_AM              0x00000004   /* accept multicast */
#define RCR_AB              0x00000008   /* accept broadcast */
#define RCR_MXDMA_UNLIMITED (7u << 8)
#define RCR_RXFTH_NONE      (7u << 13)   /* no rx threshold: forward whole frame */
#define RCR_RXBUF_64        (3u << 11)   /* rx buffer length field (RL_RXBUF_64) */

/* TCR bits. */
#define TCR_MXDMA_UNLIMITED (7u << 8)
#define TCR_IFG_NORMAL      (3u << 24)
#define TCR_HWREV_MASK      0x7CC00000u  /* stepping ID lives in TCR */

/* MISC bits (8168G and later). */
#define MISC_RXDV_GATED_EN  (1u << 19)

/*
 * Per-stepping quirks, transcribed from NetBSD rtl8169.c's hwrev switch.
 * Only the ones that change INITIALISATION are modelled; NOJUMBO, DESCV2,
 * NOEECMD and PHYWAKE_PM do not apply to a driver that does no jumbo, no
 * offload, no EEPROM access and no power management.
 */
#define Q_MACSTAT           0x0001   /* set MACSTAT_DIS, and TXENB only */
#define Q_RXDV_GATED        0x0002   /* must clear MISC.RXDV_GATED_EN */
#define Q_TXRXEN_LATER      0x0004   /* enable CR TE|RE after TCR/RCR */
#define Q_EARLYOFF          0x0008   /* RCR early-off (8168E-VL, 8168F) */
#define Q_EARLYOFFV2        0x0010   /* RCR early-off v2 (8168G and later) */

/* RCR early-off bits. */
#define RCR_EARLYOFF        0x00003800u
#define RCR_EARLYOFFV2      0x00000800u

/* CPCR MACSTAT_DIS. */
#define CPCR_MACSTAT_DIS    0x0080

/* CPCR (C+ command) bits.  TXENB/RXENB enable the DESCRIPTOR engine and are
 * distinct from the CR RE/TE bits, which drive the legacy 8139-style path. */
#define CPCR_TXENB          0x0001
#define CPCR_RXENB          0x0002
#define CPCR_PCI_MUL_RW     0x0008

/* Descriptor opts1 bits.  Length lives in bits 0..13. */
#define DESC_OWN            0x80000000u   /* NIC owns this descriptor */
#define DESC_EOR            0x40000000u   /* end of ring */
#define DESC_FS             0x20000000u   /* first segment */
#define DESC_LS             0x10000000u   /* last segment */
/* Buffer size we PROGRAM is a 13-bit field; the frame length the gigE parts
 * REPORT is 14 bits (they stole the frame-alignment bit for it). */
#define DESC_BUFLEN_MASK    0x00001FFFu
#define DESC_FRAGLEN_MASK   0x00003FFFu

/*
 * Receive status bits.  On the gigabit parts these sit ONE BIT HIGHER than on
 * the 8139C+ -- Realtek removed the frame-alignment bit to widen the length
 * field and shifted everything below FS/LS up.  OWN/EOR/FS/LS did not move.
 * Shift the word right by one before testing these, exactly as FreeBSD's
 * re(4) and NetBSD's rtl8169 do.
 */
#define RXSTAT_RXERRSUM     0x00100000u
#define RXSTAT_RUNT         0x00080000u
#define RXSTAT_CRCERR       0x00040000u

#define R8168_RX_DESCS      64
#define R8168_TX_DESCS      32
#define R8168_BUF_SIZE      2048
#define R8168_MAX_FRAME     1518

/* 16 bytes, and the ring base must be 256-byte aligned -- page-aligned DMA
 * memory satisfies that with room to spare. */
struct r8168_desc {
    uint32_t opts1;
    uint32_t opts2;
    uint32_t addr_lo;
    uint32_t addr_hi;
} __attribute__((packed));

static struct {
    volatile uint8_t  *mmio;
    uint8_t            irq;
    struct r8168_desc *rx_ring;
    uint32_t           rx_ring_phys;
    struct r8168_desc *tx_ring;
    uint32_t           tx_ring_phys;
    uint8_t           *rx_buf;
    uint32_t           rx_buf_phys;
    uint8_t           *tx_buf;
    uint32_t           tx_buf_phys;
    uint32_t           rx_cur;
    uint32_t           tx_cur;
    uint32_t           hwrev;
    uint32_t           quirks;
    netdev_t           netdev;
    int                registered;
} rt;

/* IRQ-safe: the ISR re-enters the transmit path through netdev_rx ->
 * inet_eth_input -> arp_input -> eth_send, so an inbound ARP request sends
 * its reply from inside the handler.  Same hazard as RTL-02. */
static spinlock_t rt_tx_lock = SPINLOCK_INIT("r8168_tx");

static inline uint8_t  rt_r8(uint32_t o)  { return *(volatile uint8_t  *)(rt.mmio + o); }
static inline uint16_t rt_r16(uint32_t o) { return *(volatile uint16_t *)(rt.mmio + o); }
static inline uint32_t rt_r32(uint32_t o) { return *(volatile uint32_t *)(rt.mmio + o); }
static inline void rt_w8(uint32_t o, uint8_t v)   { *(volatile uint8_t  *)(rt.mmio + o) = v; }
static inline void rt_w16(uint32_t o, uint16_t v) { *(volatile uint16_t *)(rt.mmio + o) = v; }
static inline void rt_w32(uint32_t o, uint32_t v) { *(volatile uint32_t *)(rt.mmio + o) = v; }

/* ----- RX path ----- */

static void r8168_rx_drain(void) {
    for (;;) {
        struct r8168_desc *d = &rt.rx_ring[rt.rx_cur];
        uint32_t opts1 = d->opts1;

        /* OWN set means the NIC still owns it -- nothing to collect. */
        if (opts1 & DESC_OWN)
            break;

        uint32_t len = opts1 & DESC_FRAGLEN_MASK;

        /*
         * `len` and the status bits are device-supplied.
         *
         * The error bits live one place higher than the 8139C+ layout the
         * datasheet tables are written against, so shift before testing them
         * -- FreeBSD re(4) and NetBSD rtl8169 both do exactly this.  Testing
         * them unshifted reads the WRONG bits and lets CRC-errored frames
         * through as good data.
         *
         * Accept only a descriptor that is both first and last segment (we
         * never configured scatter receive) with no error summary and a
         * plausible length.  Recycle either way, so one bad frame cannot
         * wedge the receiver -- the failure mode RTL-04 documents.
         *
         * The FCS is included in `len` (RxCRC stripping is not enabled on
         * this part), so drop the trailing 4 bytes.
         */
        uint32_t stat = opts1 >> 1;
        if ((opts1 & (DESC_FS | DESC_LS)) == (DESC_FS | DESC_LS) &&
            (stat & RXSTAT_RXERRSUM) == 0 &&
            len >= 18 && len <= R8168_MAX_FRAME + 4) {
            netdev_rx(&rt.netdev, rt.rx_buf + rt.rx_cur * R8168_BUF_SIZE,
                      len - 4);
        } else {
            rt.netdev.rx_dropped++;
        }

        /* Hand the descriptor back: OWN, buffer size, and EOR on the last
         * slot so the NIC wraps instead of running off the end. */
        uint32_t eor = (rt.rx_cur == R8168_RX_DESCS - 1) ? DESC_EOR : 0;
        d->opts2   = 0;
        d->opts1   = DESC_OWN | eor | (R8168_BUF_SIZE & DESC_BUFLEN_MASK);

        rt.rx_cur = (rt.rx_cur + 1) % R8168_RX_DESCS;
    }
}

static int r8168_irq(unsigned int irq, void *dev_id, void *frame) {
    (void)irq; (void)dev_id; (void)frame;

    uint16_t isr = rt_r16(R_ISR);
    if (isr == 0)
        return 0;                    /* not ours: shared line */

    /* ISR is write-1-to-clear.  Acknowledge before processing so an event
     * arriving during the drain is not lost. */
    rt_w16(R_ISR, isr);

    if (isr & (INT_ROK | INT_RER | INT_RDU | INT_FOVW))
        r8168_rx_drain();

    /*
     * RDU means the NIC ran out of descriptors it owned.  The drain above
     * has just handed them all back, but the receiver needs a nudge to go
     * look again.
     */
    if (isr & (INT_RDU | INT_FOVW))
        rt_w8(R_CR, CR_TE | CR_RE);

    /* TOK/TER need no work here: the transmit path reclaims by testing OWN
     * on the descriptor it is about to reuse. */
    return 1;
}

/* ----- TX path ----- */

static int r8168_xmit(netdev_t *dev, const void *frame, size_t len) {
    (void)dev;
    if (!frame || len == 0) return -EINVAL;
    if (len > R8168_MAX_FRAME) return -EMSGSIZE;

    unsigned long flags = spinlock_acquire_irq(&rt_tx_lock);

    uint32_t slot = rt.tx_cur;
    struct r8168_desc *d = &rt.tx_ring[slot];

    /*
     * Do not reuse a descriptor the NIC still owns; its buffer is being
     * DMA'd.  Bounded so a wedged NIC returns an error rather than spinning
     * forever with interrupts disabled.
     */
    if (d->opts1 & DESC_OWN) {
        int spins = 0;
        while (d->opts1 & DESC_OWN) {
            if (++spins > 1000000) {
                spinlock_release_irq(&rt_tx_lock, flags);
                rt.netdev.tx_dropped++;
                return -EIO;
            }
        }
    }

    memcpy(rt.tx_buf + slot * R8168_BUF_SIZE, frame, len);

    /*
     * The hardware pads to the 60-byte minimum itself only when told to; be
     * explicit instead of relying on it, so a short frame cannot go out with
     * whatever the buffer held last time.  The buffer was zeroed at setup and
     * we only ever overwrite the first `len` bytes, so pad by extending the
     * programmed length over known-zero memory.
     */
    uint32_t xlen = (uint32_t)len;
    if (xlen < 60) {
        memset(rt.tx_buf + slot * R8168_BUF_SIZE + len, 0, 60 - len);
        xlen = 60;
    }

    uint32_t eor = (slot == R8168_TX_DESCS - 1) ? DESC_EOR : 0;
    d->addr_lo = rt.tx_buf_phys + slot * R8168_BUF_SIZE;
    d->addr_hi = 0;
    d->opts2   = 0;
    /* OWN last: everything else must be visible to the NIC first. */
    d->opts1   = DESC_OWN | DESC_FS | DESC_LS | eor |
                 (xlen & DESC_BUFLEN_MASK);

    rt.tx_cur = (slot + 1) % R8168_TX_DESCS;

    rt_w8(R_TPPOLL, TPPOLL_NPQ);      /* go look at the ring */

    spinlock_release_irq(&rt_tx_lock, flags);
    return 0;
}

static const struct netdev_ops r8168_ops = {
    .xmit = r8168_xmit,
};

/* ----- stepping identification ----- */

/*
 * The stepping ID is in the TX config register, not PCI config space: one PCI
 * ID (0x8168) covers fifteen years of silicon whose init sequences differ.
 * Values and quirk assignments transcribed from NetBSD rtl8169.c.
 */
static const struct {
    uint32_t hwrev;
    uint32_t quirks;
    const char *name;
} r8168_hwrevs[] = {
    /* 8168B: MACSTAT only. */
    { 0x30000000, Q_MACSTAT, "8168B" },
    { 0x38000000, Q_MACSTAT, "8168B" },
    { 0x38400000, Q_MACSTAT, "8168B" },
    /* 8168C/CP/D/DP. */
    { 0x3C000000, Q_MACSTAT, "8168C" },
    { 0x3C400000, Q_MACSTAT, "8168C" },
    { 0x3C800000, Q_MACSTAT, "8168CP" },
    { 0x28000000, Q_MACSTAT, "8168D" },
    { 0x28800000, Q_MACSTAT, "8168DP" },
    /* 8168E. */
    { 0x2C000000, Q_MACSTAT, "8168E" },
    /* 8168E-VL and 8168F add the early-off receive tweak. */
    { 0x2C800000, Q_MACSTAT | Q_EARLYOFF, "8168E-VL" },
    { 0x48000000, Q_MACSTAT | Q_EARLYOFF, "8168F" },
    { 0x48800000, Q_MACSTAT, "8411" },
    /*
     * 8168G and later -- the generation on any 2013+ board, which is what a
     * Haswell Lenovo C460 will have.  These need RXDV gating cleared and the
     * TX/RX enable moved AFTER the config registers, and like every MACSTAT
     * part they take TXENB WITHOUT RXENB in the C+ command word.
     */
    { 0x4C000000, Q_MACSTAT | Q_RXDV_GATED | Q_TXRXEN_LATER | Q_EARLYOFFV2, "8168G" },
    { 0x4C100000, Q_MACSTAT | Q_RXDV_GATED | Q_TXRXEN_LATER | Q_EARLYOFFV2, "8168G" },
    { 0x50000000, Q_MACSTAT | Q_RXDV_GATED | Q_TXRXEN_LATER | Q_EARLYOFFV2, "8168EP" },
    { 0x50800000, Q_MACSTAT | Q_RXDV_GATED | Q_TXRXEN_LATER | Q_EARLYOFFV2, "8168GU" },
    { 0x50900000, Q_MACSTAT | Q_RXDV_GATED | Q_TXRXEN_LATER | Q_EARLYOFFV2, "8168G" },
    { 0x54000000, Q_MACSTAT | Q_RXDV_GATED | Q_TXRXEN_LATER | Q_EARLYOFFV2, "8168H" },
    { 0x54100000, Q_MACSTAT | Q_RXDV_GATED | Q_TXRXEN_LATER | Q_EARLYOFFV2, "8168H" },
    { 0x54800000, Q_MACSTAT | Q_RXDV_GATED | Q_TXRXEN_LATER | Q_EARLYOFFV2, "8168FP" },
    { 0x5C800000, Q_MACSTAT | Q_RXDV_GATED | Q_TXRXEN_LATER | Q_EARLYOFFV2, "8411B" },
    { 0, 0, NULL },
};

static const char *r8168_identify(void) {
    rt.hwrev = rt_r32(R_TCR) & TCR_HWREV_MASK;
    for (int i = 0; r8168_hwrevs[i].name != NULL; i++) {
        if (r8168_hwrevs[i].hwrev == rt.hwrev) {
            rt.quirks = r8168_hwrevs[i].quirks;
            return r8168_hwrevs[i].name;
        }
    }
    /*
     * Unknown stepping.  Assume the MODERN behaviour rather than the ancient
     * one: everything from the 8168B onwards wants MACSTAT, and every part
     * new enough not to be in this table is newer than 8168G.  Guessing "old"
     * for a new chip sets RXENB on a part that must not have it.
     */
    rt.quirks = Q_MACSTAT | Q_RXDV_GATED | Q_TXRXEN_LATER | Q_EARLYOFFV2;
    return NULL;
}

/* ----- setup ----- */

static int r8168_setup(pci_device_t *pdev) {
    if (rt.registered) {
        kprint("r8168: additional controller ignored (single instance)\n");
        return -1;
    }

    /* Memory space + bus mastering.  Without BME the rings are never read. */
    uint16_t cmd = pci_read_config16(pdev->bus, pdev->slot, pdev->func,
                                     PCI_CONFIG_COMMAND);
    pci_write_config16(pdev->bus, pdev->slot, pdev->func, PCI_CONFIG_COMMAND,
                       cmd | 0x0002 | 0x0004);

    /*
     * BAR2 is the MMIO window on the 8168 (BAR0 is a legacy I/O alias that
     * not every variant implements).  Fall back to BAR1 for the handful of
     * boards that place it there.
     */
    rt.mmio = pci_iomap(pdev, 2, 0x1000);
    if (!rt.mmio)
        rt.mmio = pci_iomap(pdev, 1, 0x1000);
    if (!rt.mmio) {
        kprint("r8168: could not map MMIO BAR\n");
        return -1;
    }

    /* Soft reset and wait for the chip to clear RST itself. */
    rt_w8(R_CR, CR_RST);
    int spins = 0;
    while (rt_r8(R_CR) & CR_RST) {
        if (++spins > 1000000) {
            kprint("r8168: reset timed out\n");
            return -1;
        }
    }

    rt_w8(R_CFG9346, CFG9346_UNLOCK);

    /* Identify the stepping BEFORE configuring anything: the quirks it
     * selects change the C+ command word and the enable ordering. */
    const char *revname = r8168_identify();

    /* MAC out of IDR0..5.  Loaded from the EEPROM by the chip at reset. */
    for (int i = 0; i < 6; i++)
        rt.netdev.hwaddr[i] = rt_r8(R_IDR0 + i);

    /* Drop any stale multicast filter. */
    rt_w32(R_MAR0, 0);
    rt_w32(R_MAR0 + 4, 0);

    /* DMA memory.  pmm_alloc_contiguous returns a direct-mapped VIRTUAL
     * address; the NIC needs the physical one. */
    size_t rx_ring_bytes = R8168_RX_DESCS * sizeof(struct r8168_desc);
    size_t tx_ring_bytes = R8168_TX_DESCS * sizeof(struct r8168_desc);
    size_t rx_buf_bytes  = R8168_RX_DESCS * R8168_BUF_SIZE;
    size_t tx_buf_bytes  = R8168_TX_DESCS * R8168_BUF_SIZE;
    void *p;

    p = pmm_alloc_contiguous((rx_ring_bytes + 4095) / 4096);
    if (!p) { kprint("r8168: rx ring alloc failed\n"); return -1; }
    memset(p, 0, ((rx_ring_bytes + 4095) / 4096) * 4096);
    rt.rx_ring = p;
    rt.rx_ring_phys = (uint32_t)(uintptr_t)p - 0xC0000000u;

    p = pmm_alloc_contiguous((tx_ring_bytes + 4095) / 4096);
    if (!p) { kprint("r8168: tx ring alloc failed\n"); return -1; }
    memset(p, 0, ((tx_ring_bytes + 4095) / 4096) * 4096);
    rt.tx_ring = p;
    rt.tx_ring_phys = (uint32_t)(uintptr_t)p - 0xC0000000u;

    p = pmm_alloc_contiguous((rx_buf_bytes + 4095) / 4096);
    if (!p) { kprint("r8168: rx buffer alloc failed\n"); return -1; }
    memset(p, 0, ((rx_buf_bytes + 4095) / 4096) * 4096);
    rt.rx_buf = p;
    rt.rx_buf_phys = (uint32_t)(uintptr_t)p - 0xC0000000u;

    p = pmm_alloc_contiguous((tx_buf_bytes + 4095) / 4096);
    if (!p) { kprint("r8168: tx buffer alloc failed\n"); return -1; }
    memset(p, 0, ((tx_buf_bytes + 4095) / 4096) * 4096);
    rt.tx_buf = p;
    rt.tx_buf_phys = (uint32_t)(uintptr_t)p - 0xC0000000u;

    /* Every RX descriptor starts owned by the NIC; the last carries EOR. */
    for (int i = 0; i < R8168_RX_DESCS; i++) {
        rt.rx_ring[i].addr_lo = rt.rx_buf_phys + (uint32_t)i * R8168_BUF_SIZE;
        rt.rx_ring[i].addr_hi = 0;
        rt.rx_ring[i].opts2   = 0;
        rt.rx_ring[i].opts1   = DESC_OWN | R8168_BUF_SIZE |
                                ((i == R8168_RX_DESCS - 1) ? DESC_EOR : 0);
    }
    /* TX descriptors start owned by US (OWN clear), EOR on the last. */
    for (int i = 0; i < R8168_TX_DESCS; i++) {
        rt.tx_ring[i].opts1 = (i == R8168_TX_DESCS - 1) ? DESC_EOR : 0;
        rt.tx_ring[i].opts2 = 0;
    }
    rt.rx_cur = 0;
    rt.tx_cur = 0;

    /*
     * Programming order follows the datasheet (and Linux's r8169): sizes and
     * config registers, then the C+ command word, then the descriptor bases,
     * then enable, then re-lock, then the receive filter, then interrupts.
     * The 8168 latches some of these only while CFG9346 is unlocked, and the
     * receive filter must be written AFTER RE is set or the first frames are
     * dropped.
     */
    /*
     * The C+ command register comes FIRST -- "we must configure the C+
     * register before all others" (NetBSD rtl8169.c).  It carries the
     * DESCRIPTOR-engine enables, which are a different thing from the CR
     * TE|RE bits below (those drive the legacy 8139-style datapath).
     *
     * The enable bits are stepping-dependent and NOT symmetric: on every
     * MACSTAT part -- which is everything from the 8168B onwards, including
     * the 8168G generation in any recent machine -- the word is
     * MACSTAT_DIS|TXENB with RXENB deliberately ABSENT.  Setting RXENB there
     * is wrong.  Only the pre-8168B parts take RXENB|TXENB.
     */
    uint16_t cpcr = CPCR_PCI_MUL_RW;
    if (rt.quirks & Q_MACSTAT)
        cpcr |= CPCR_MACSTAT_DIS | CPCR_TXENB;
    else
        cpcr |= CPCR_RXENB | CPCR_TXENB;
    rt_w16(R_CPCR, cpcr);

    /* The BSDs sleep 10ms here before touching anything else. */
    for (volatile int i = 0; i < 1000000; i++) { }

    rt_w16(R_RMS, R8168_BUF_SIZE);            /* accept up to a full buffer */

    /*
     * Descriptor bases, HIGH half first -- that is the order both BSDs use,
     * and on a part with a 64-bit register pair the low write is what the
     * chip latches on.
     */
    rt_w32(R_TNPDS + 4, 0);
    rt_w32(R_TNPDS,     rt.tx_ring_phys);
    rt_w32(R_RDSAR + 4, 0);
    rt_w32(R_RDSAR,     rt.rx_ring_phys);

    /*
     * 8168G and later gate the receive data valid signal after reset and will
     * receive nothing until it is ungated.  Harmless to skip on older parts,
     * fatal to skip on new ones.
     */
    if (rt.quirks & Q_RXDV_GATED)
        rt_w32(R_MISC, rt_r32(R_MISC) & ~MISC_RXDV_GATED_EN);

    /*
     * Enable ordering is stepping-dependent.  Most parts want TE|RE set
     * before TCR/RCR; the 8168G generation wants it AFTER (RTKQ_TXRXEN_LATER).
     */
    if (!(rt.quirks & Q_TXRXEN_LATER))
        rt_w8(R_CR, CR_TE | CR_RE);

    rt_w32(R_TCR, TCR_MXDMA_UNLIMITED | TCR_IFG_NORMAL);
    rt_w8(R_ETHRESH, 16);                     /* early TX threshold */

    uint32_t rcr = RCR_MXDMA_UNLIMITED | RCR_RXFTH_NONE | RCR_RXBUF_64;
    if (rt.quirks & Q_EARLYOFF)
        rcr |= RCR_EARLYOFF;
    else if (rt.quirks & Q_EARLYOFFV2)
        rcr |= RCR_EARLYOFFV2;
    rt_w32(R_RCR, rcr);

    if (rt.quirks & Q_TXRXEN_LATER)
        rt_w8(R_CR, CR_TE | CR_RE);

    /*
     * Receive filter: OR the accept bits into whatever the chip reports, so
     * we do not clobber fields the part set for itself.  No AAP -- the stack
     * does not want other stations' traffic.
     */
    rt_w32(R_RCR, rt_r32(R_RCR) | RCR_APM | RCR_AB | RCR_AM);

    rt_w8(R_CFG9346, CFG9346_LOCK);

    rt_w16(R_ISR, 0xFFFF);                    /* clear anything latched */

    /* Shared PCI INTx, and the result is checked: a NIC with no handler is a
     * dead interface whose only symptom shows up much later as ENODEV. */
    rt.irq = (uint8_t)pci_get_irq(pdev);
    if (rt.irq) {
        int rc = request_irq(rt.irq, r8168_irq, IRQF_SHARED, "r8168", &rt);
        if (rc != 0) {
            kprintf("r8168: could not install IRQ %u handler (%d)%s\n",
                    (unsigned)rt.irq, rc,
                    rc == -EBUSY ? " - line held exclusively by another driver"
                                 : "");
            return -1;
        }
    }

    /* Matches RL_INTRS_CPLUS / RTK_INTRS_CPLUS, including SERR: a PCI system
     * error is exactly the thing you want to hear about on first bring-up. */
    rt_w16(R_IMR, INT_ROK | INT_RER | INT_TOK | INT_TER |
                  INT_RDU | INT_FOVW | INT_LINKCHG | INT_SERR);

    strlcpy(rt.netdev.name, "eth0", NETDEV_NAME_MAX);
    rt.netdev.mtu = 1500;
    rt.netdev.flags = NETDEV_IFF_UP | NETDEV_IFF_BROADCAST | NETDEV_IFF_RUNNING;
    rt.netdev.ops = &r8168_ops;
    rt.netdev.driver_data = &rt;
    netdev_register(&rt.netdev);
    rt.registered = 1;

    /* PHYstatus is worth printing on first bring-up: if the link never comes
     * up, this says whether the PHY negotiated at all. */
    kprintf("r8168: %s (hwrev 0x%08x quirks 0x%x) "
            "%02x:%02x:%02x:%02x:%02x:%02x irq %u phy 0x%02x\n",
            revname ? revname : "UNKNOWN stepping",
            (unsigned)rt.hwrev, (unsigned)rt.quirks,
            rt.netdev.hwaddr[0], rt.netdev.hwaddr[1], rt.netdev.hwaddr[2],
            rt.netdev.hwaddr[3], rt.netdev.hwaddr[4], rt.netdev.hwaddr[5],
            (unsigned)rt.irq, (unsigned)rt_r8(R_PHYSTATUS));
    if (revname == NULL) {
        /* Say so loudly: the quirk guess is the most likely reason a
         * bring-up on new silicon misbehaves, and hwrev above is exactly
         * what a new table entry needs. */
        kprint("r8168: stepping not in table, assuming 8168G-class quirks\n");
    }
    return 0;
}

/*
 * 0x8168 covers the RTL8111/8168 family across all its steppings; 0x8161 and
 * 0x8136 are the other PCIe Realtek IDs that speak the same C+ descriptor
 * interface (0x8136 is the Fast Ethernet RTL8101/8102).  0x8129 and 0x8139
 * are the OLD PCI parts and belong to rtl8139.c, not here.
 */
static const device_id_t r8168_ids[] = {
    { R8168_VENDOR, 0x8168, 0, 0, 0 },   /* RTL8111/8168/8411 - the C460 */
    { R8168_VENDOR, 0x8161, 0, 0, 0 },   /* RTL8111/8168 variant */
    { R8168_VENDOR, 0x8136, 0, 0, 0 },   /* RTL8101/8102 Fast Ethernet */
    { 0, 0, 0, 0, 0 },
};

static int r8168_pci_attach(struct device *dev) {
    pci_device_t *pdev = pci_find_device_by_kdev(dev);
    if (!pdev) return -1;
    return r8168_setup(pdev);
}

static int r8168_pci_detach(struct device *dev) { (void)dev; return 0; }

static struct driver r8168_pci_driver = {
    .name = "r8168-pci",
    .id_table = r8168_ids,
    .attach = r8168_pci_attach,
    .detach = r8168_pci_detach,
};

void r8168_init(void) {
    static int registered;
    if (!registered) {
        (void)driver_register(&r8168_pci_driver, &pci_bus_type);
        registered = 1;
    }
}
