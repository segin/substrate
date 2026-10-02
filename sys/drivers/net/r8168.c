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
#include <kern/time.h>
#include <sys/irq.h>
#include <sys/lock.h>
#include <sys/netdev.h>
#include <sys/random.h>
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
#define R_PHYAR             0x60   /* PHY access, pre-8168G */
#define R_PHYSTATUS         0x6C
#define R_PMCH              0x6F   /* power management, 8-bit */
#define R_GPHY_OCP          0xB8   /* PHY access window, 8168G and later */
#define R_CFG_D1            0xD1
#define R_TIMERINT          0x58   /* timer interrupt threshold, 32-bit */
#define R_RMS               0xDA   /* rx max packet size, 16-bit */
#define R_INTRMIT           0xE2   /* interrupt mitigation, 16-bit */
#define R_CPCR              0xE0   /* C+ command, 16-bit */
#define R_RDSAR             0xE4   /* RX desc base, 64-bit */
#define R_ETHRESH           0xEC   /* early TX threshold, 8-bit */

/* CR bits. */
#define CR_TE               0x04
#define CR_RE               0x08
#define CR_RST              0x10

/* TPPoll bits. */
#define TPPOLL_NPQ          0x40   /* kick the normal-priority queue */
#define TPPOLL_FSWINT       0x01   /* force a software interrupt (ISR.SWInt) */

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
#define INT_SWINT           0x0100   /* software interrupt, from TPPOLL_FSWINT */
#define INT_TIMEOUT         0x4000
#define INT_SERR            0x8000   /* PCI system error */

/* Normal interrupt set, including SERR: a PCI system error is exactly the
 * thing you want to hear about.  TDU is in it so a doorbell the transmitter
 * missed is rung again (r8168_tx_rekick()), and SWINT so the handler can
 * come back for frames its receive budget left behind. */
#define R8168_IMR           (INT_ROK | INT_RER | INT_TOK | INT_TER | \
                             INT_RDU | INT_FOVW | INT_LINKCHG | INT_TDU | \
                             INT_SWINT | INT_SERR)

/* Where the interrupt comes from (rt.irq_kind), in order of preference. */
#define R8168_IRQ_NONE      0
#define R8168_IRQ_MSI       1   /* vector from irq_alloc_vector() */
#define R8168_IRQ_LINE      2   /* firmware Interrupt Line, via the PIC */
#define R8168_IRQ_ROUTED    3   /* pci_route_intx(): I/O APIC by convention */

/* How long to wait for the forced software interrupt to arrive. */
#define R8168_IRQ_PROBE_MS  50
/* How long the vector must stay silent before and after the forced one. */
#define R8168_IRQ_QUIET_MS  10

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

/* PHYAR / GPHY_OCP: bit 31 is the busy/complete flag; it reads back set
 * when a read has completed and clear when a write has. */
#define PHYAR_FLAG          0x80000000u
#define PHY_ACCESS_US       2000         /* generous; 20 us is typical */
#define PMCH_PHY_POWER      0x80

/* PHYstatus. */
#define PHYS_FDX            0x01
#define PHYS_LINK           0x02
#define PHYS_10M            0x04
#define PHYS_100M           0x08
#define PHYS_1000M          0x10

/* PHY registers (IEEE 802.3 clause 22, plus the Realtek page select). */
#define MII_BMCR            0x00
#define MII_ANAR            0x04
#define MII_CTRL1000        0x09
#define MII_PWRSAVE         0x0E
#define MII_PAGE            0x1F
#define BMCR_RESET          0x8000
#define BMCR_ANENABLE       0x1000
#define BMCR_ANRESTART      0x0200
#define ANAR_10_100_ALL     0x01E0       /* 10/100, half and full */
#define ANAR_PAUSE          0x0C00       /* symmetric + asymmetric pause */
#define ANAR_CSMA           0x0001
#define CTRL1000_ADV        0x0300       /* 1000BASE-T half and full */
#define PHY_RESET_MS        500

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
#define Q_PHY_OCP           0x0020   /* PHY through the OCP window, not PHYAR */
#define Q_PMCH              0x0040   /* PHY power gated by PMCH bit 7 */
#define Q_FASTETH           0x0080   /* 10/100 only (810xE) */
#define Q_8401E             0x0100   /* 8401E: also clear 0xD1 bit 3 */
#define Q_PHY_PWRSAVE       0x0200   /* 8168 PHY: clear reg 0x0E power save */

/* The 8168G generation and everything after it. */
#define Q_GEN_G             (Q_MACSTAT | Q_RXDV_GATED | Q_TXRXEN_LATER | \
                             Q_EARLYOFFV2 | Q_PHY_OCP | Q_PHY_PWRSAVE)

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

/* Frames collected per interrupt.  The receiver refills a descriptor the
 * moment it is handed back, so an unbounded drain under a flood never ends;
 * whatever is left stays latched in ISR and raises the next interrupt. */
#define R8168_RX_BUDGET     64

/* Service passes per interrupt before handing the CPU back. */
#define R8168_ISR_PASSES    4

/* The descriptors live in DMA memory the NIC writes behind the compiler's
 * back; on x86 a compiler barrier is all the ordering they need. */
#define r8168_barrier()     __asm__ __volatile__("" ::: "memory")

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
    int                irq;          /* vector / line; -1: none */
    int                irq_kind;     /* R8168_IRQ_* */
    volatile uint32_t  intr_count;   /* interrupts that were ours */
    volatile uint32_t  intr_calls;   /* every handler invocation */
    volatile uint16_t  imr;          /* the mask the handler restores */
    volatile struct r8168_desc *rx_ring;
    uint32_t           rx_ring_phys;
    volatile struct r8168_desc *tx_ring;
    uint32_t           tx_ring_phys;
    uint8_t           *rx_buf;
    uint32_t           rx_buf_phys;
    uint8_t           *tx_buf;
    uint32_t           tx_buf_phys;
    uint32_t           rx_cur;
    uint32_t           tx_cur;
    uint32_t           hwrev;
    uint32_t           quirks;
    uint32_t           ocp_base;     /* PHY page, as an OCP address */
    uint8_t            link;         /* last reported PHYstatus link bit */
    netdev_t           netdev;
    int                registered;
} rt;

/* IRQ-safe: the ISR re-enters the transmit path through netdev_rx ->
 * inet_eth_input -> arp_input -> eth_send, so an inbound ARP request sends
 * its reply from inside the handler.  Same hazard as rtl8139's TX path. */
static spinlock_t rt_tx_lock = SPINLOCK_INIT("r8168_tx");

static inline uint8_t  rt_r8(uint32_t o)  { return *(volatile uint8_t  *)(rt.mmio + o); }
static inline uint16_t rt_r16(uint32_t o) { return *(volatile uint16_t *)(rt.mmio + o); }
static inline uint32_t rt_r32(uint32_t o) { return *(volatile uint32_t *)(rt.mmio + o); }
static inline void rt_w8(uint32_t o, uint8_t v)   { *(volatile uint8_t  *)(rt.mmio + o) = v; }
static inline void rt_w16(uint32_t o, uint16_t v) { *(volatile uint16_t *)(rt.mmio + o) = v; }
static inline void rt_w32(uint32_t o, uint32_t v) { *(volatile uint32_t *)(rt.mmio + o) = v; }

static void r8168_report_link(void);

/* ----- RX path ----- */

/* Returns non-zero if the budget ran out with frames still waiting. */
static int r8168_rx_drain(int budget) {
    for (;;) {
        volatile struct r8168_desc *d = &rt.rx_ring[rt.rx_cur];
        uint32_t opts1 = d->opts1;

        /* OWN set means the NIC still owns it -- nothing to collect. */
        if (opts1 & DESC_OWN)
            return 0;
        if (budget-- <= 0)
            return 1;

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
         * wedge the receiver -- the failure mode rtl8139's rtl_rx_reset()
         * documents.
         *
         * The FCS is included in `len` (RxCRC stripping is not enabled on
         * this part), so drop the trailing 4 bytes.
         */
        uint32_t stat = opts1 >> 1;
        r8168_barrier();             /* buffer contents after the status */
        if ((opts1 & (DESC_FS | DESC_LS)) == (DESC_FS | DESC_LS) &&
            (stat & RXSTAT_RXERRSUM) == 0 &&
            len >= 18 && len <= R8168_MAX_FRAME + 4) {
            netdev_rx(&rt.netdev, rt.rx_buf + rt.rx_cur * R8168_BUF_SIZE,
                      len - 4);
        } else {
            rt.netdev.rx_dropped++;
        }

        /* Hand the descriptor back: OWN, buffer size, and EOR on the last
         * slot so the NIC wraps instead of running off the end.  OWN goes
         * in the same store as everything else in opts1, after opts2. */
        uint32_t eor = (rt.rx_cur == R8168_RX_DESCS - 1) ? DESC_EOR : 0;
        d->opts2   = 0;
        r8168_barrier();
        d->opts1   = DESC_OWN | eor | (R8168_BUF_SIZE & DESC_BUFLEN_MASK);

        rt.rx_cur = (rt.rx_cur + 1) % R8168_RX_DESCS;
    }
}

/*
 * A doorbell write that lands while the transmitter is busy can be ignored
 * by the PCIe parts, stranding whatever was queued behind it until the next
 * unrelated send.  Ring it again on every TX completion while the newest
 * queued descriptor is still owned by the NIC.
 */
static void r8168_tx_rekick(void) {
    unsigned long flags = spinlock_acquire_irq(&rt_tx_lock);
    uint32_t last = (rt.tx_cur + R8168_TX_DESCS - 1) % R8168_TX_DESCS;

    if (rt.tx_ring[last].opts1 & DESC_OWN)
        rt_w8(R_TPPOLL, TPPOLL_NPQ);
    spinlock_release_irq(&rt_tx_lock, flags);
}

static int r8168_irq(unsigned int irq, void *dev_id, void *frame) {
    (void)irq; (void)dev_id; (void)frame;
    int pass, more = 0;

    rt.intr_calls++;

    /*
     * All-ones is a device that has dropped off the bus: not ours, and no
     * register writes.  Bits latched but masked in IMR are not an interrupt
     * this device raised, so on a shared line they are someone else's.
     */
    uint16_t isr = rt_r16(R_ISR);
    if (isr == 0xFFFF || (isr & rt.imr) == 0)
        return 0;
    rt.intr_count++;                 /* proof of life for r8168_setup_irq() */

    /*
     * In MSI mode a message is sent only when (ISR & IMR) goes from zero to
     * non-zero.  An event latching between the ISR read and its acknowledge
     * would keep that AND non-zero forever and no further message would ever
     * come.  So mask everything while servicing, re-read until nothing is
     * pending, and restore IMR at the end: a bit still pending then makes a
     * fresh edge, and the IMR write also pushes the posted acknowledge out
     * ahead of the EOI on a level-triggered line.
     */
    rt_w16(R_IMR, 0);
    for (pass = 0; pass < R8168_ISR_PASSES; pass++) {
        isr &= rt.imr;
        if (isr == 0)
            break;
        rt_w16(R_ISR, isr);          /* write-1-to-clear */

        /* Only once eth0 exists: the interrupt probe runs before
         * registration, and a frame arriving then has nowhere to go. */
        more = 0;
        if (rt.registered &&
            (isr & (INT_ROK | INT_RER | INT_RDU | INT_FOVW | INT_SWINT)))
            more = r8168_rx_drain(R8168_RX_BUDGET);

        /* RDU means the NIC ran out of descriptors it owned.  The drain has
         * handed them back, but the receiver needs a nudge to look again. */
        if (isr & (INT_RDU | INT_FOVW))
            rt_w8(R_CR, CR_TE | CR_RE);

        if (rt.registered && (isr & (INT_TOK | INT_TDU)))
            r8168_tx_rekick();

        if (rt.registered && (isr & INT_LINKCHG))
            r8168_report_link();

        isr = rt_r16(R_ISR);
        if (isr == 0xFFFF)
            return 1;
    }
    rt_w16(R_IMR, rt.imr);
    /*
     * Frames left over by the budget were already acknowledged, so nothing
     * latched would bring us back for them before the next arrival.  A
     * forced software interrupt does, after the EOI, giving everything else
     * a turn first.
     */
    if (more && rt.registered)
        rt_w8(R_TPPOLL, TPPOLL_FSWINT);
    (void)rt_r16(R_IMR);
    return 1;
}

/* ----- interrupt setup ----- */

/*
 * Install the handler on one interrupt source.  0 on success, -1 if this
 * source is unavailable (no MSI capability, no firmware line, no I/O APIC)
 * or could not be claimed.
 */
static int r8168_irq_install(pci_device_t *pdev, int kind) {
    int irq;

    switch (kind) {
    case R8168_IRQ_MSI:
        if (pci_find_capability(pdev, PCI_CAP_ID_MSI) == 0)
            return -1;
        irq = irq_alloc_vector();
        break;
    case R8168_IRQ_LINE:
        irq = pci_get_irq(pdev);
        /* INTx Disable survives from firmware, or from an earlier routed
         * attempt's teardown; the pin never asserts while it is set. */
        if (irq >= 0)
            pci_intx_enable(pdev, 1);
        break;
    case R8168_IRQ_ROUTED:
        irq = pci_route_intx(pdev);
        break;
    default:
        return -1;
    }
    if (irq < 0)
        return -1;

    if (request_irq((unsigned int)irq, r8168_irq, IRQF_SHARED, "r8168", &rt) != 0) {
        kprintf("r8168: could not install IRQ %d handler\n", irq);
        if (kind == R8168_IRQ_MSI) {
            irq_free_vector(irq);
        } else if (kind == R8168_IRQ_ROUTED) {
            pci_unroute_intx(pdev, irq);
            irq_free_vector(irq);
        }
        return -1;
    }
    /* MSI goes on only once something is there to take it. */
    if (kind == R8168_IRQ_MSI && pci_enable_msi(pdev, (uint8_t)irq) != 0) {
        free_irq((unsigned int)irq, &rt);
        irq_free_vector(irq);
        return -1;
    }
    rt.irq = irq;
    rt.irq_kind = kind;
    return 0;
}

static void r8168_irq_remove(pci_device_t *pdev) {
    if (rt.irq_kind == R8168_IRQ_NONE)
        return;
    rt.imr = 0;
    rt_w16(R_IMR, 0);
    if (rt.irq_kind == R8168_IRQ_MSI)
        (void)pci_disable_msi(pdev);
    free_irq((unsigned int)rt.irq, &rt);
    if (rt.irq_kind == R8168_IRQ_MSI) {
        irq_free_vector(rt.irq);
    } else if (rt.irq_kind == R8168_IRQ_ROUTED) {
        pci_unroute_intx(pdev, rt.irq);
        irq_free_vector(rt.irq);
    }
    rt.irq = -1;
    rt.irq_kind = R8168_IRQ_NONE;
}

/* Force a software interrupt and report whether the handler saw it. */
/*
 * SWInt latches in ISR whichever route the interrupt takes, so seeing it
 * from inside the handler proves nothing on its own: on a misrouted or
 * shared line, any foreign invocation during the wait would find it there.
 * The proof is therefore three-sided: the vector is quiet with our mask
 * empty, an invocation follows the forced interrupt, and the vector is
 * quiet again once it is acknowledged and masked.
 */
static int r8168_irq_quiet(void) {
    uint32_t before = rt.intr_calls;

    timer_busywait_ms(R8168_IRQ_QUIET_MS);
    return rt.intr_calls == before;
}

static int r8168_irq_proven(void) {
    uint32_t before;
    unsigned int ms;

    rt.imr = 0;
    rt_w16(R_IMR, 0);
    rt_w16(R_ISR, 0xFFFF);
    (void)rt_r16(R_ISR);
    if (!r8168_irq_quiet())
        return 0;                    /* something else is driving it */

    before = rt.intr_count;
    rt.imr = INT_SWINT;
    rt_w16(R_IMR, INT_SWINT);
    rt_w8(R_TPPOLL, TPPOLL_FSWINT);
    for (ms = 0; ms < R8168_IRQ_PROBE_MS && rt.intr_count == before; ms++)
        timer_busywait_ms(1);
    rt.imr = 0;
    rt_w16(R_IMR, 0);
    rt_w16(R_ISR, 0xFFFF);
    (void)rt_r16(R_ISR);
    if (rt.intr_count == before)
        return 0;
    return r8168_irq_quiet();
}

/*
 * Find an interrupt that actually arrives.  Receive is interrupt-driven, so
 * without one the interface transmits and never hears anything back.
 *
 * MSI first: it goes straight to the local APIC.  Under UEFI the firmware
 * leaves PCI Interrupt Line at 0xFF, and the driver used to store
 * pci_get_irq()'s "none" (-1) in a uint8_t, hook the resulting IRQ 255 and
 * register an eth0 that could never receive.  Then the firmware line, then
 * pci_route_intx()'s conventional I/O APIC routing.
 *
 * Each source is proven with a forced software interrupt.  If none can be
 * proven -- a stepping whose SWInt behaves differently would look like
 * that -- the first source that installs is kept anyway, with a warning,
 * rather than refusing an interrupt that may well work.
 */
static int r8168_setup_irq(pci_device_t *pdev) {
    static const int kinds[] = { R8168_IRQ_MSI, R8168_IRQ_LINE, R8168_IRQ_ROUTED };
    static const char *const names[] = { "none", "MSI", "IRQ line", "routed INTx" };
    size_t i;

    rt.irq = -1;
    rt.irq_kind = R8168_IRQ_NONE;

    for (i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
        if (r8168_irq_install(pdev, kinds[i]) != 0)
            continue;
        if (r8168_irq_proven()) {
            kprintf("r8168: %s %d verified\n", names[kinds[i]], rt.irq);
            return 0;
        }
        kprintf("r8168: no interrupt on %s %d\n", names[kinds[i]], rt.irq);
        r8168_irq_remove(pdev);
    }

    for (i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
        if (r8168_irq_install(pdev, kinds[i]) == 0) {
            kprintf("r8168: WARNING: using unverified %s %d\n",
                    names[kinds[i]], rt.irq);
            return 0;
        }
    }
    kprint("r8168: no usable interrupt; receive cannot work\n");
    return -1;
}

/* ----- TX path ----- */

static int r8168_xmit(netdev_t *dev, const void *frame, size_t len) {
    (void)dev;
    if (!frame || len == 0) return -EINVAL;
    if (len > R8168_MAX_FRAME) return -EMSGSIZE;

    unsigned long flags = spinlock_acquire_irq(&rt_tx_lock);

    uint32_t slot = rt.tx_cur;
    volatile struct r8168_desc *d = &rt.tx_ring[slot];

    /*
     * The NIC clears OWN as each frame completes, so a next slot it still
     * owns means the whole ring is in flight.  That is back-pressure, not
     * something to wait out with interrupts disabled: drop the frame now and
     * let the upper layer retransmit.  A transmitter that has stopped
     * altogether is the watchdog's business.
     */
    if (d->opts1 & DESC_OWN) {
        rt_w8(R_TPPOLL, TPPOLL_NPQ);
        spinlock_release_irq(&rt_tx_lock, flags);
        rt.netdev.tx_dropped++;
        return -ENOBUFS;
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
    /* OWN last: the buffer and every other field must be visible to the
     * NIC first, and the descriptor before the doorbell. */
    r8168_barrier();
    d->opts1   = DESC_OWN | DESC_FS | DESC_LS | eor |
                 (xlen & DESC_BUFLEN_MASK);

    rt.tx_cur = (slot + 1) % R8168_TX_DESCS;

    r8168_barrier();
    rt_w8(R_TPPOLL, TPPOLL_NPQ);      /* go look at the ring */

    spinlock_release_irq(&rt_tx_lock, flags);
    return 0;
}

/*
 * RCR_AM filters multicast against the 64-bit MAR hash.  Setup opens it
 * fully and it stays open whatever IPv4 groups come and go: IPv6 neighbour
 * discovery (ff02::1 and the solicited-node groups) needs it with no IPv4
 * group joined at all.  The IP layer filters by membership.
 */
static void r8168_set_allmulti(netdev_t *dev, int on) {
    (void)dev; (void)on;
    rt_w32(R_MAR0, 0xFFFFFFFFu);
    rt_w32(R_MAR0 + 4, 0xFFFFFFFFu);
}

/* Station address: IDR0-5 as two 32-bit writes, high half first, with
 * CFG9346 unlocked (as Linux's rtl_rar_set does). */
static int r8168_set_hwaddr(netdev_t *dev, const uint8_t mac[6]) {
    (void)dev;
    rt_w8(R_CFG9346, CFG9346_UNLOCK);
    rt_w32(R_IDR0 + 4, (uint32_t)mac[4] | (uint32_t)mac[5] << 8);
    rt_w32(R_IDR0, (uint32_t)mac[0] | (uint32_t)mac[1] << 8 |
                   (uint32_t)mac[2] << 16 | (uint32_t)mac[3] << 24);
    rt_w8(R_CFG9346, CFG9346_LOCK);
    return 0;
}

static const struct netdev_ops r8168_ops = {
    .xmit = r8168_xmit,
    .set_allmulti = r8168_set_allmulti,
    .set_hwaddr = r8168_set_hwaddr,
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
    { 0x30000000, Q_MACSTAT | Q_PHY_PWRSAVE, "8168B" },
    { 0x38000000, Q_MACSTAT | Q_PHY_PWRSAVE, "8168B" },
    { 0x38400000, Q_MACSTAT | Q_PHY_PWRSAVE, "8168B" },
    /* 8168C/CP/D/DP.  The D/DP gate the PHY's power through PMCH. */
    { 0x3C000000, Q_MACSTAT | Q_PHY_PWRSAVE, "8168C" },
    { 0x3C400000, Q_MACSTAT | Q_PHY_PWRSAVE, "8168C" },
    { 0x3C800000, Q_MACSTAT | Q_PHY_PWRSAVE, "8168CP" },
    { 0x28000000, Q_MACSTAT | Q_PHY_PWRSAVE | Q_PMCH, "8168D" },
    { 0x28800000, Q_MACSTAT | Q_PHY_PWRSAVE | Q_PMCH, "8168DP" },
    /* 8168E. */
    { 0x2C000000, Q_MACSTAT | Q_PHY_PWRSAVE | Q_PMCH, "8168E" },
    /* 8168E-VL and 8168F add the early-off receive tweak. */
    { 0x2C800000, Q_MACSTAT | Q_PHY_PWRSAVE | Q_EARLYOFF, "8168E-VL" },
    { 0x48000000, Q_MACSTAT | Q_PHY_PWRSAVE | Q_EARLYOFF, "8168F" },
    { 0x48800000, Q_MACSTAT | Q_PHY_PWRSAVE, "8411" },
    /*
     * 8168G and later -- the generation on any 2013+ board, which is what a
     * Haswell Lenovo C460 will have.  These need RXDV gating cleared and the
     * TX/RX enable moved AFTER the config registers, and like every MACSTAT
     * part they take TXENB WITHOUT RXENB in the C+ command word.  Their PHY
     * is reached through the OCP window.
     */
    { 0x4C000000, Q_GEN_G, "8168G" },
    { 0x4C100000, Q_GEN_G, "8168G" },
    { 0x50000000, Q_GEN_G, "8168EP" },
    { 0x50800000, Q_GEN_G, "8168GU" },
    { 0x50900000, Q_GEN_G, "8168G" },
    { 0x54000000, Q_GEN_G, "8168H" },
    { 0x54100000, Q_GEN_G, "8168H" },
    { 0x54800000, Q_GEN_G, "8168FP" },
    { 0x5C800000, Q_GEN_G, "8411B" },
    { 0, 0, NULL },
};

/*
 * The 810xE Fast Ethernet parts (PCI ID 0x8136) speak the same descriptor
 * interface but predate the 8168G generation: none of them takes the RXDV
 * gate or the late TE/RE enable.  The oldest want RXENB in the C+ command
 * word like a pre-8168B part, and several gate their PHY's power through
 * PMCH.  The RTL8106E-US/8107E report 8168GU/8168H revisions and are found
 * in the table above.
 */
static const struct {
    uint32_t hwrev;
    uint32_t quirks;
    const char *name;
} r810x_hwrevs[] = {
    { 0x30800000, Q_FASTETH, "8100E" },
    { 0x38800000, Q_FASTETH, "8100E" },
    { 0x34000000, Q_FASTETH, "8101E" },
    { 0x34800000, Q_FASTETH | Q_MACSTAT, "8102E" },
    { 0x24800000, Q_FASTETH | Q_MACSTAT, "8102EL" },
    { 0x24C00000, Q_FASTETH | Q_MACSTAT, "8102EL" },
    { 0x34C00000, Q_FASTETH | Q_MACSTAT, "8103E" },
    { 0x24000000, Q_FASTETH | Q_MACSTAT | Q_PMCH | Q_8401E, "8401E" },
    { 0x40800000, Q_FASTETH | Q_MACSTAT | Q_PMCH, "8105E" },
    { 0x40C00000, Q_FASTETH | Q_MACSTAT | Q_PMCH, "8105E" },
    { 0x44000000, Q_FASTETH | Q_MACSTAT | Q_PMCH, "8402" },
    { 0x44800000, Q_FASTETH | Q_MACSTAT | Q_PMCH, "8106E" },
    { 0, 0, NULL },
};

static const char *r8168_identify(pci_device_t *pdev) {
    rt.hwrev = rt_r32(R_TCR) & TCR_HWREV_MASK;
    for (int i = 0; r8168_hwrevs[i].name != NULL; i++) {
        if (r8168_hwrevs[i].hwrev == rt.hwrev) {
            rt.quirks = r8168_hwrevs[i].quirks;
            return r8168_hwrevs[i].name;
        }
    }
    if (pdev->device_id == 0x8136) {
        for (int i = 0; r810x_hwrevs[i].name != NULL; i++) {
            if (r810x_hwrevs[i].hwrev == rt.hwrev) {
                rt.quirks = r810x_hwrevs[i].quirks;
                return r810x_hwrevs[i].name;
            }
        }
        /* An unknown Fast Ethernet part is an older design, not a newer
         * one: the pre-8168G sequence, with TE/RE before TCR/RCR. */
        rt.quirks = Q_FASTETH | Q_MACSTAT;
        return NULL;
    }
    /*
     * Unknown gigabit stepping.  Assume the MODERN behaviour rather than the
     * ancient one: everything from the 8168B onwards wants MACSTAT, and
     * every part new enough not to be in this table is newer than 8168G.
     * Guessing "old" for a new chip sets RXENB on a part that must not have
     * it.
     */
    rt.quirks = Q_GEN_G;
    return NULL;
}

/* ----- PHY ----- */

static void r8168_phyar_wait(uint32_t reg, uint32_t want) {
    for (unsigned us = 0; us < PHY_ACCESS_US; us += 20) {
        if ((rt_r32(reg) & PHYAR_FLAG) == want)
            return;
        timer_busywait_us(20);
    }
}

/*
 * 8168G and later reach the PHY's registers as OCP addresses: page 0's
 * standard registers sit at 0xA400 + 2 * reg, and writing register 0x1F
 * moves the window to another page instead of reaching the PHY at all.
 */
static uint32_t r8168_ocp_addr(int reg) {
    if (rt.ocp_base != 0xA400)
        reg -= 0x10;
    return rt.ocp_base + (uint32_t)reg * 2;
}

static uint16_t r8168_phy_read(int reg) {
    if (rt.quirks & Q_PHY_OCP) {
        if (reg == MII_PAGE)
            return (uint16_t)(rt.ocp_base == 0xA400 ? 0 : rt.ocp_base >> 4);
        rt_w32(R_GPHY_OCP, r8168_ocp_addr(reg) << 15);
        r8168_phyar_wait(R_GPHY_OCP, PHYAR_FLAG);
        return (uint16_t)rt_r32(R_GPHY_OCP);
    }
    rt_w32(R_PHYAR, (uint32_t)(reg & 0x1F) << 16);
    r8168_phyar_wait(R_PHYAR, PHYAR_FLAG);
    return (uint16_t)rt_r32(R_PHYAR);
}

static void r8168_phy_write(int reg, uint16_t val) {
    if (rt.quirks & Q_PHY_OCP) {
        if (reg == MII_PAGE) {
            rt.ocp_base = val ? (uint32_t)val << 4 : 0xA400;
            return;
        }
        rt_w32(R_GPHY_OCP, PHYAR_FLAG | r8168_ocp_addr(reg) << 15 | val);
        r8168_phyar_wait(R_GPHY_OCP, 0);
        return;
    }
    rt_w32(R_PHYAR, PHYAR_FLAG | (uint32_t)(reg & 0x1F) << 16 | val);
    r8168_phyar_wait(R_PHYAR, 0);
}

/*
 * CR.RST resets the MAC only.  A PHY left powered down -- by an OS shutdown
 * path with WoL off, a firmware "LAN off" setting or a stopped network stack
 * -- stays down across it, and the link never comes up.  Power the PHY, take
 * it out of power save, reset it (which clears power-down and isolate),
 * advertise everything it can do and restart autonegotiation.
 */
static void r8168_phy_bringup(void) {
    unsigned ms;

    if (rt.quirks & Q_PMCH) {
        rt_w8(R_PMCH, rt_r8(R_PMCH) | PMCH_PHY_POWER);
        if (rt.quirks & Q_8401E)
            rt_w8(R_CFG_D1, rt_r8(R_CFG_D1) & ~0x08);
        timer_busywait_ms(1);
    }

    rt.ocp_base = 0xA400;
    r8168_phy_write(MII_PAGE, 0);
    if (rt.quirks & Q_PHY_PWRSAVE)
        r8168_phy_write(MII_PWRSAVE, 0);

    r8168_phy_write(MII_BMCR, BMCR_RESET);
    for (ms = 0; ms < PHY_RESET_MS; ms++) {
        if (!(r8168_phy_read(MII_BMCR) & BMCR_RESET))
            break;
        timer_busywait_ms(1);
    }
    if (ms == PHY_RESET_MS)
        kprint("r8168: PHY reset did not complete\n");

    r8168_phy_write(MII_ANAR, ANAR_10_100_ALL | ANAR_PAUSE | ANAR_CSMA);
    if (!(rt.quirks & Q_FASTETH))
        r8168_phy_write(MII_CTRL1000, CTRL1000_ADV);
    r8168_phy_write(MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART);
}

/* Follow PHYstatus into the interface's RUNNING flag, and say so. */
static void r8168_report_link(void) {
    uint8_t ps = rt_r8(R_PHYSTATUS);
    uint8_t up = ps & PHYS_LINK;

    if (up == rt.link)
        return;
    rt.link = up;
    if (up) {
        rt.netdev.flags |= NETDEV_IFF_RUNNING;
        kprintf("r8168: link up, %u Mb/s %s duplex\n",
                (ps & PHYS_1000M) ? 1000u : (ps & PHYS_100M) ? 100u : 10u,
                (ps & PHYS_FDX) ? "full" : "half");
    } else {
        rt.netdev.flags &= ~NETDEV_IFF_RUNNING;
        kprint("r8168: link down\n");
    }
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

    /* Firmware leaves ASPM L0s/L1 and CLKREQ on for its own power policy.
     * Without the chip-specific tuning those need, the link dropping into
     * L1 under the driver shows up as RX stalls and TX descriptors stuck
     * with OWN set. */
    pci_disable_aspm(pdev);

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
    const char *revname = r8168_identify(pdev);

    r8168_phy_bringup();

    /* MAC out of IDR0..5.  Loaded from the EEPROM by the chip at reset. */
    for (int i = 0; i < 6; i++)
        rt.netdev.hwaddr[i] = rt_r8(R_IDR0 + i);

    /*
     * A blank EEPROM or eFuse autoload area leaves zeros (or ones) in IDR,
     * and a group address cannot be a station address.  Registering one
     * makes DHCP and ARP fail in ways that look like a dead link, so
     * replace it with a random locally administered unicast address and
     * program that back into IDR.
     */
    {
        uint8_t *m = rt.netdev.hwaddr;
        int zero = (m[0] | m[1] | m[2] | m[3] | m[4] | m[5]) == 0;
        int ones = (m[0] & m[1] & m[2] & m[3] & m[4] & m[5]) == 0xFF;
        if (zero || ones || (m[0] & 0x01)) {
            if (random_get_bytes(m, 6) != 6)
                for (int i = 0; i < 6; i++)
                    m[i] = (uint8_t)(rt.hwrev >> (i * 4)) ^ (uint8_t)(0x5A + i);
            m[0] = (uint8_t)((m[0] & ~0x01) | 0x02);
            r8168_set_hwaddr(&rt.netdev, m);
            rt_w8(R_CFG9346, CFG9346_UNLOCK);   /* it re-locks; setup isn't done */
            kprint("r8168: no valid MAC address in IDR; using a random "
                   "locally administered one\n");
        }
    }

    /* Open the multicast hash completely and leave it open: a 64-bit hash
     * would have to cover ff02::1, every solicited-node group and
     * 224.0.0.1, and the stack filters by membership anyway. */
    rt_w32(R_MAR0, 0xFFFFFFFFu);
    rt_w32(R_MAR0 + 4, 0xFFFFFFFFu);

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

    /* Interrupt moderation and the timer interrupt keep whatever firmware
     * or a previous driver left in them; leftover thresholds delay ROK on
     * low-rate request/response traffic. */
    rt_w16(R_INTRMIT, 0);
    rt_w32(R_TIMERINT, 0);

    /* Let the C+ command settle before touching anything else. */
    timer_busywait_ms(10);

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

    /* The result is checked: a NIC with no handler is a dead interface whose
     * only symptom shows up much later as ENODEV. */
    if (r8168_setup_irq(pdev) != 0)
        return -1;

    rt.imr = R8168_IMR;
    rt_w16(R_IMR, R8168_IMR);

    strlcpy(rt.netdev.name, "eth0", NETDEV_NAME_MAX);
    rt.netdev.mtu = 1500;
    /* RUNNING follows the link (r8168_report_link()), not registration. */
    rt.netdev.flags = NETDEV_IFF_UP | NETDEV_IFF_BROADCAST |
                      NETDEV_IFF_MULTICAST;
    rt.link = 0;
    rt.netdev.ops = &r8168_ops;
    rt.netdev.driver_data = &rt;
    netdev_register(&rt.netdev);
    rt.registered = 1;

    /* PHYstatus is worth printing on first bring-up: if the link never comes
     * up, this says whether the PHY negotiated at all. */
    kprintf("r8168: %s (hwrev 0x%08x quirks 0x%x) "
            "%02x:%02x:%02x:%02x:%02x:%02x %s %d phy 0x%02x\n",
            revname ? revname : "UNKNOWN stepping",
            (unsigned)rt.hwrev, (unsigned)rt.quirks,
            rt.netdev.hwaddr[0], rt.netdev.hwaddr[1], rt.netdev.hwaddr[2],
            rt.netdev.hwaddr[3], rt.netdev.hwaddr[4], rt.netdev.hwaddr[5],
            rt.irq_kind == R8168_IRQ_MSI ? "msi" : "irq",
            rt.irq, (unsigned)rt_r8(R_PHYSTATUS));
    if (revname == NULL) {
        /* Say so loudly: the quirk guess is the most likely reason a
         * bring-up on new silicon misbehaves, and hwrev above is exactly
         * what a new table entry needs. */
        kprint((rt.quirks & Q_FASTETH)
               ? "r8168: stepping not in table, assuming 8102E-class quirks\n"
               : "r8168: stepping not in table, assuming 8168G-class quirks\n");
    }
    /* Autonegotiation was restarted moments ago, so this usually reports
     * nothing; LINKCHG brings the result. */
    r8168_report_link();
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
