/*
 * e1000e.c — Intel 82574 and PCH-integrated (I217/I218/I219) Gigabit
 * Ethernet driver.
 *
 * These parts keep the 8254x register map that e1000.c drives, but differ
 * in how they must be brought up:
 *
 *   82574L / 82583V   discrete PCIe NICs; qemu's `-device e1000e`.
 *   I217 / I218       the MAC inside Lynx Point / Wildcat Point PCHs, with
 *                     an external PHY on the PCH's MDIO bus (address 2).
 *   I219              the same from Sunrise Point on, plus a reset hang
 *                     when descriptors are left pending (see
 *                     e1k2_flush_desc_rings()).
 *
 * Data path: one RX and one TX ring of LEGACY 16-byte descriptors.  Every
 * part here still supports them as long as RFCTL.EXSTEN is clear, which the
 * driver forces -- a PXE ROM may have left extended descriptors enabled.
 *
 * Bring-up follows Linux's e1000e and iPXE's intel driver:
 *   - MAC reset only, never PHY reset, on the PCH parts: the Management
 *     Engine may own the PHY (FWSM.RSPCIPHY), and iPXE found PHY reset
 *     unreliable on them.
 *   - The station address is the RAR0 the hardware loads from NVM; these
 *     parts keep their NVM in SPI flash, not behind EERD.
 *   - CTRL_EXT.DRV_LOAD tells manageability firmware (AMT on vPro) that a
 *     driver owns the port now.
 *   - PHY access through MDIC under the EXTCNF_CTRL software flag, which
 *     arbitrates the MDIO bus with firmware.  Used to power the PHY up if a
 *     previous OS left it powered down, and to log its ID -- a PHY that does
 *     not answer is the signature of I217+ Ultra Low Power mode, which this
 *     driver does not yet know how to leave.
 *
 * Not covered: the older PCH PHYs (82577/82578 on Ibex Peak, 82579 on
 * Cougar Point / Panther Point), which need per-generation PHY workarounds.
 *
 * QEMU: -netdev user,id=n0 -device e1000e,netdev=n0
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
#include <vm/vm_kmem.h>

#define E1K2_VENDOR         0x8086

/* Register offsets (BAR0, MMIO). */
#define R_CTRL              0x0000
#define R_STATUS            0x0008
#define R_EECD              0x0010
#define R_CTRL_EXT          0x0018
#define R_MDIC              0x0020
#define R_ICR               0x00C0   /* interrupt cause read (read clears) */
#define R_ICS               0x00C8   /* interrupt cause set */
#define R_IMS               0x00D0   /* interrupt mask set */
#define R_IMC               0x00D8   /* interrupt mask clear */
#define R_RCTL              0x0100
#define R_EXTCNF_CTRL       0x0F00
#define R_TCTL              0x0400
#define R_TIPG              0x0410
#define R_KABGTXD           0x3004
#define R_RDBAL             0x2800
#define R_RDBAH             0x2804
#define R_RDLEN             0x2808
#define R_RDH               0x2810
#define R_RDT               0x2818
#define R_RXDCTL            0x2828
#define R_TDBAL             0x3800
#define R_TDBAH             0x3804
#define R_TDLEN             0x3808
#define R_TDH               0x3810
#define R_TDT               0x3818
#define R_TXDCTL            0x3828
#define R_RFCTL             0x5008
#define R_MTA               0x5200   /* multicast table array, 128 dwords */
#define R_RAL0              0x5400
#define R_RAH0              0x5404
#define R_FWSM              0x5B54
#define R_FEXTNVM11         0x5BBC
#define R_FEXTNVM7          0x00E4
#define R_FEXTNVM9          0x5BB4
#define R_ITR               0x00C4   /* interrupt throttle, 256 ns units */

/* FEXTNVM7 / FEXTNVM9 (Sunrise Point and later). */
#define FEXTNVM7_SIDE_CLK_UNGATE      0x00000004u
#define FEXTNVM9_IOSFSB_CLKGATE_DIS   0x00000800u
#define FEXTNVM9_IOSFSB_CLKREQ_DIS    0x00001000u

/* Interrupt throttling off (see E1K2_IMS), set explicitly so a rate left
 * by firmware or a previous driver does not linger. */
#define E1K2_ITR            0

#define RAH_AV              0x80000000u   /* Address Valid */

/* CTRL bits. */
#define CTRL_GIO_MASTER_DISABLE (1u << 2)
#define CTRL_LRST           (1u << 3)
#define CTRL_ASDE           (1u << 5)
#define CTRL_SLU            (1u << 6)
#define CTRL_FRCSPD         (1u << 11)
#define CTRL_FRCDPLX        (1u << 12)
#define CTRL_RST            (1u << 26)

/* STATUS bits. */
#define STATUS_FD           (1u << 0)
#define STATUS_LU           (1u << 1)
#define STATUS_SPEED_SHIFT  6
#define STATUS_SPEED_MASK   (3u << STATUS_SPEED_SHIFT)
#define STATUS_GIO_MASTER_ENABLE (1u << 19)

/* EECD (82574). */
#define EECD_AUTO_RD        (1u << 9)    /* NVM auto-read done */

/* CTRL_EXT bits. */
#define CTRL_EXT_BIT22      (1u << 22)   /* required set on 82571+/ICH */
#define CTRL_EXT_DRV_LOAD   (1u << 28)

/* MDIC. */
#define MDIC_REG_SHIFT      16
#define MDIC_PHY_SHIFT      21
#define MDIC_OP_WRITE       0x04000000u
#define MDIC_OP_READ        0x08000000u
#define MDIC_READY          0x10000000u
#define MDIC_ERROR          0x40000000u

/* EXTCNF_CTRL: software ownership of the MDIO bus / shared resources. */
#define EXTCNF_SWFLAG       0x00000020u

/* FWSM. */
#define FWSM_RSPCIPHY       0x00000040u   /* PHY reset allowed */
#define FWSM_FW_VALID       0x00008000u   /* manageability firmware present */

/* FEXTNVM11 (I219). */
#define FEXTNVM11_DISABLE_MULR_FIX 0x00002000u

/* KABGTXD. */
#define KABGTXD_BGSQLBIAS   0x00050000u

/* RCTL bits. */
#define RCTL_EN             (1u << 1)
#define RCTL_MPE            (1u << 4)    /* multicast promiscuous */
#define RCTL_BAM            (1u << 15)   /* accept broadcast */
#define RCTL_BSIZE_2048     (0u << 16)
#define RCTL_SECRC          (1u << 26)   /* strip Ethernet CRC */

/* RFCTL. */
#define RFCTL_EXSTEN        (1u << 15)   /* extended RX status (descriptors) */

/* TCTL bits. */
#define TCTL_EN             (1u << 1)
#define TCTL_PSP            (1u << 3)    /* pad short packets */
#define TCTL_CT_SHIFT       4            /* collision threshold */
#define TCTL_COLD_SHIFT     12           /* collision distance */
#define TCTL_RTLC           (1u << 24)   /* retransmit on late collision */

/* TXDCTL. */
#define TXDCTL_WTHRESH_1    (1u << 16)   /* write back each descriptor */
#define TXDCTL_GRAN         (1u << 24)   /* thresholds count descriptors */
#define TXDCTL_BIT22        (1u << 22)   /* required set on 82571+/ICH */

/* RXDCTL. */
#define RXDCTL_THRESH_UNIT_DESC (1u << 24)  /* thresholds count descriptors */

/* Interrupt cause/mask bits. */
#define ICR_TXDW            (1u << 0)
#define ICR_LSC             (1u << 2)
#define ICR_RXDMT0          (1u << 4)
#define ICR_RXO             (1u << 6)
#define ICR_RXT0            (1u << 7)
#define ICR_INT_ASSERTED    (1u << 31)   /* 82571+: this device asserted INTx */

/*
 * TXDW stays unmasked although transmit reclaims by testing DD.  Measured
 * under QEMU with bulk TCP uploads: masking it, or throttling the vector
 * through ITR, delayed ACK processing enough that uploads intermittently
 * fell into retransmit backoff and stalled -- with no frame lost by this
 * driver in either direction.  The stack's loss recovery is that sensitive
 * to ACK latency, so this driver takes every interrupt as it comes.
 */
#define E1K2_IMS            (ICR_RXT0 | ICR_RXDMT0 | ICR_RXO | ICR_TXDW | ICR_LSC)

/* The rings live in DMA memory the MAC writes behind the compiler's back;
 * on x86 a compiler barrier is all the ordering they need. */
#define e2k_barrier()       __asm__ __volatile__("" ::: "memory")

/* Legacy receive descriptor status bits. */
#define RXD_STAT_DD         0x01
#define RXD_STAT_EOP        0x02

/* Legacy transmit descriptor command/status bits. */
#define TXD_CMD_EOP         0x01
#define TXD_CMD_IFCS        0x02
#define TXD_CMD_RS          0x08
#define TXD_STAT_DD         0x01

/* PHY (IEEE 802.3 clause 22). */
#define PHY_BMCR            0x00
#define PHY_ID1             0x02
#define PHY_ID2             0x03
#define BMCR_ANRESTART      0x0200
#define BMCR_PDOWN          0x0800
#define BMCR_ANENABLE       0x1000

/* PCI config: I219 descriptor-ring status (Linux PCICFG_DESC_RING_STATUS). */
#define PCICFG_DESC_RING_STATUS    0xE4
#define FLUSH_DESC_REQUIRED        0x0100

/* Deep enough to absorb a burst arriving within one throttled interrupt
 * interval (ITR) without overrunning: a dropped ACK burst costs TCP a
 * retransmit timeout. */
#define E1K2_RX_DESCS       256
/* Room for a whole TCP window in flight, so a full ring is the exception. */
#define E1K2_TX_DESCS       128
/* How long a sender with interrupts on may wait for a free slot: several
 * full-size frame times at 10 Mb/s. */
#define E1K2_TX_WAIT_US     5000
#define E1K2_TX_POLL_US     20
#define E1K2_BUF_SIZE       2048
#define E1K2_MAX_FRAME      1518

#define E1K2_RESET_MS       20
#define E1K2_MASTER_POLLS   800          /* x 100 us */
#define E1K2_SWFLAG_MS      1000
#define E1K2_MDIC_MS        20
#define E1K2_IRQ_PROBE_MS   50
#define E1K2_IRQ_QUIET_MS   10

/* Chip classes (driver_data in the ID table). */
#define K_82574             1   /* 82574L / 82583V: discrete, PHY at 1 */
#define K_LPT               2   /* I217 / I218: PCH MAC, PHY at 2 */
#define K_SPT               3   /* I219: as LPT, plus the reset hang */

/* Where the interrupt comes from, in order of preference. */
#define IRQ_NONE_K          0
#define IRQ_MSI             1
#define IRQ_LINE            2
#define IRQ_ROUTED          3

struct e1k2_rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t csum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed));

struct e1k2_tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed));

static struct {
    volatile uint8_t     *mmio;
    pci_device_t         *pdev;
    int                   kind;
    int                   irq;
    int                   irq_kind;
    volatile uint32_t     intr_count;    /* interrupts that were ours */
    volatile uint32_t     intr_calls;    /* every handler invocation */
    volatile uint32_t     lsc_count;     /* ours, with LSC in ICR */
    volatile struct e1k2_rx_desc *rx_ring;
    uint32_t              rx_ring_phys;
    volatile struct e1k2_tx_desc *tx_ring;
    uint32_t              tx_ring_phys;
    uint8_t              *rx_buf;
    uint32_t              rx_buf_phys;
    uint8_t              *tx_buf;
    uint32_t              tx_buf_phys;
    uint32_t              rx_cur;
    uint32_t              tx_cur;
    uint32_t              link_status;   /* last reported STATUS & LU */
    netdev_t              netdev;
    int                   registered;
} e2k;

/* IRQ-safe for the same reason as e1000's: an inbound ARP request sends its
 * reply from inside the RX interrupt. */
static spinlock_t e2k_tx_lock = SPINLOCK_INIT("e1000e_tx");

static inline uint32_t e2k_read(uint32_t reg) {
    return *(volatile uint32_t *)(e2k.mmio + reg);
}

static inline void e2k_write(uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(e2k.mmio + reg) = val;
}

/* Posted MMIO writes: read something back to push them out. */
static inline void e2k_flush(void) {
    (void)e2k_read(R_STATUS);
}

static int e2k_is_pch(void) {
    return e2k.kind == K_LPT || e2k.kind == K_SPT;
}

/* ----- MDIO / PHY ----- */

/*
 * EXTCNF_CTRL.SWFLAG arbitrates the MDIO bus (and, on the PCH, the PHY
 * configuration) between this driver, manageability firmware and the
 * hardware's own NVM loader.  Wait for any other owner to let go, then take
 * it and confirm the bit stuck -- firmware can refuse it.
 */
static int e2k_swflag_acquire(void) {
    unsigned ms;
    uint32_t v;

    for (ms = 0; (e2k_read(R_EXTCNF_CTRL) & EXTCNF_SWFLAG) != 0; ms++) {
        if (ms >= E1K2_SWFLAG_MS)
            return -1;
        timer_busywait_ms(1);
    }
    /*
     * Writing the bit is a request the arbiter may grant later, so write it
     * once and poll.  If it is not granted in time, withdraw it: a grant
     * arriving after we gave up would otherwise leave the flag owned for
     * the rest of the boot, locking manageability firmware and the
     * hardware's PHY configuration loader out of the PHY.
     */
    v = e2k_read(R_EXTCNF_CTRL);
    e2k_write(R_EXTCNF_CTRL, v | EXTCNF_SWFLAG);
    for (ms = 0; ; ms++) {
        if (e2k_read(R_EXTCNF_CTRL) & EXTCNF_SWFLAG)
            return 0;
        if (ms >= E1K2_SWFLAG_MS) {
            e2k_write(R_EXTCNF_CTRL, e2k_read(R_EXTCNF_CTRL) & ~EXTCNF_SWFLAG);
            return -1;
        }
        timer_busywait_ms(1);
    }
}

static void e2k_swflag_release(void) {
    e2k_write(R_EXTCNF_CTRL, e2k_read(R_EXTCNF_CTRL) & ~EXTCNF_SWFLAG);
}

static int e2k_phy_addr(void) {
    return e2k.kind == K_82574 ? 1 : 2;
}

/* One MDIC transaction; caller holds the software flag. */
static int e2k_mdic(uint32_t op, int reg, uint16_t wdata, uint16_t *rdata) {
    uint32_t v;
    unsigned ms;

    e2k_write(R_MDIC, op | ((uint32_t)reg << MDIC_REG_SHIFT) |
                      ((uint32_t)e2k_phy_addr() << MDIC_PHY_SHIFT) | wdata);
    for (ms = 0; ; ms++) {
        v = e2k_read(R_MDIC);
        if (v & MDIC_READY)
            break;
        if (ms >= E1K2_MDIC_MS)
            return -1;
        timer_busywait_ms(1);
    }
    if (v & MDIC_ERROR)
        return -1;
    if (rdata)
        *rdata = (uint16_t)v;
    return 0;
}

static int e2k_phy_read(int reg, uint16_t *val) {
    int rc;

    if (e2k_swflag_acquire() != 0)
        return -2;
    rc = e2k_mdic(MDIC_OP_READ, reg, 0, val);
    e2k_swflag_release();
    return rc;
}

static int e2k_phy_write(int reg, uint16_t val) {
    int rc;

    if (e2k_swflag_acquire() != 0)
        return -2;
    rc = e2k_mdic(MDIC_OP_WRITE, reg, val, NULL);
    e2k_swflag_release();
    return rc;
}

/*
 * Make sure the PHY is powered and negotiating.  Best effort: a PHY this
 * driver cannot reach may still be linked up by firmware, so failures are
 * reported, not fatal.
 */
static void e2k_phy_bringup(void) {
    uint16_t id1 = 0, id2 = 0, bmcr = 0;
    int rc;

    rc = e2k_phy_read(PHY_ID1, &id1);
    if (rc == 0)
        rc = e2k_phy_read(PHY_ID2, &id2);
    if (rc == 0)
        rc = e2k_phy_read(PHY_BMCR, &bmcr);
    if (rc == -2) {
        kprint("e1000e: MDIO bus held by firmware; PHY left as found\n");
        return;
    }
    if (rc != 0) {
        kprint("e1000e: PHY not responding on MDIO (Ultra Low Power mode?); "
               "left as found\n");
        return;
    }
    kprintf("e1000e: PHY id %04x:%04x, BMCR 0x%04x\n",
            (unsigned)id1, (unsigned)id2, (unsigned)bmcr);

    /* A previous OS (or its suspend path) may have powered the PHY down;
     * the link can never come up like that. */
    if (bmcr & BMCR_PDOWN) {
        bmcr = (uint16_t)((bmcr & ~BMCR_PDOWN) | BMCR_ANENABLE | BMCR_ANRESTART);
        if (e2k_phy_write(PHY_BMCR, bmcr) == 0)
            kprint("e1000e: PHY was powered down; powered up, "
                   "autonegotiation restarted\n");
        else
            kprint("e1000e: PHY is powered down and could not be woken\n");
    }
}

/* ----- RX path ----- */

/*
 * Collect up to one ring's worth of frames.  The MAC refills each
 * descriptor as soon as it is handed back, so a drain that runs until the
 * ring is empty never ends under a flood -- and it runs in the interrupt
 * handler, starving the timer and everything else.  Returns non-zero if
 * frames were left for later.
 */
static int e2k_rx_drain(void) {
    int budget = E1K2_RX_DESCS;

    /* RDT trails our cursor by one: tail must never equal head, or the
     * hardware reads the ring as full and stops receiving. */
    for (;;) {
        volatile struct e1k2_rx_desc *d = &e2k.rx_ring[e2k.rx_cur];
        if (!(d->status & RXD_STAT_DD))
            return 0;
        if (budget-- <= 0)
            return 1;
        e2k_barrier();               /* length and buffer after DD */

        uint16_t len = d->length;

        /* Length and error bits come from the device: drop anything that is
         * not a whole, clean, sane frame, but always recycle the descriptor
         * so one bad frame cannot wedge the ring. */
        if ((d->status & RXD_STAT_EOP) && d->errors == 0 &&
            len >= 14 && len <= E1K2_MAX_FRAME) {
            netdev_rx(&e2k.netdev, e2k.rx_buf + e2k.rx_cur * E1K2_BUF_SIZE,
                      len);
        } else {
            e2k.netdev.rx_dropped++;
        }

        d->status = 0;
        uint32_t prev = e2k.rx_cur;
        e2k.rx_cur = (e2k.rx_cur + 1) % E1K2_RX_DESCS;
        e2k_barrier();               /* descriptor recycled before the tail */
        e2k_write(R_RDT, prev);
    }
}

static void e2k_report_link(void) {
    static const unsigned speeds[4] = { 10, 100, 1000, 1000 };
    uint32_t st = e2k_read(R_STATUS);
    uint32_t lu = st & STATUS_LU;

    if (lu == e2k.link_status)
        return;
    e2k.link_status = lu;
    if (lu) {
        kprintf("e1000e: link up, %u Mb/s %s duplex\n",
                speeds[(st & STATUS_SPEED_MASK) >> STATUS_SPEED_SHIFT],
                (st & STATUS_FD) ? "full" : "half");
    } else {
        kprint("e1000e: link down\n");
    }
}

static int e2k_irq(unsigned int irq, void *dev_id, void *frame) {
    (void)irq; (void)dev_id; (void)frame;

    /* ICR is read-to-clear.  On a shared INTx line, 82571+ parts say
     * whether they asserted it; without INT_ASSERTED the causes are not
     * ours to act on (and IMS did not auto-mask).  MSI is never shared. */
    e2k.intr_calls++;
    uint32_t icr = e2k_read(R_ICR);
    /* All-ones is a function that has stopped responding: not ours, and
     * nothing in it to act on. */
    if (icr == 0 || icr == 0xFFFFFFFFu)
        return 0;
    if (e2k.irq_kind != IRQ_MSI && !(icr & ICR_INT_ASSERTED))
        return 0;
    e2k.intr_count++;
    if (icr & ICR_LSC)
        e2k.lsc_count++;

    /* The interrupt probe runs before the netdev exists. */
    if (!e2k.registered)
        return 1;

    /* Frames left by the budget: ICR is read-to-clear, so re-raise the
     * cause.  It arrives after the EOI (and the ITR gap), letting the
     * timer and everything else in first. */
    if ((icr & (ICR_RXT0 | ICR_RXDMT0 | ICR_RXO)) && e2k_rx_drain())
        e2k_write(R_ICS, ICR_RXT0);
    if (icr & ICR_LSC)
        e2k_report_link();
    return 1;
}

/* ----- TX path ----- */

/* A slot is free once its last frame has retired (or it was never used). */
static int e2k_tx_slot_busy(uint32_t slot) {
    volatile struct e1k2_tx_desc *d = &e2k.tx_ring[slot];
    return d->cmd != 0 && !(d->status & TXD_STAT_DD);
}

static int e2k_xmit(netdev_t *dev, const void *frame, size_t len) {
    const uint8_t *f = frame;
    (void)dev;
    if (!frame || len == 0) return -EINVAL;

    /*
     * The MAC appends the FCS (IFCS), so the buffer itself may be at most
     * 1514 bytes, or 1518 with an 802.1Q tag in bytes 12-13.
     */
    size_t max = (len >= 14 && f[12] == 0x81 && f[13] == 0x00)
                 ? E1K2_MAX_FRAME : E1K2_MAX_FRAME - 4;
    if (len > max) return -EMSGSIZE;

    unsigned long flags;
    uint32_t slot, next;
    unsigned waited_us = 0;

    for (;;) {
        flags = spinlock_acquire_irq(&e2k_tx_lock);

        /*
         * The MAC stops transmit DMA while the link is down, so anything
         * queued then would only sit in the ring; drop it now and let the
         * upper layer retry once there is a link.  The link state is the
         * one LSC keeps current: a STATUS read here would put an MMIO
         * round trip in every frame's path, inside the IRQ-off lock.
         */
        if (e2k.link_status != STATUS_LU) {
            spinlock_release_irq(&e2k_tx_lock, flags);
            e2k.netdev.tx_dropped++;
            return -ENETDOWN;
        }

        slot = e2k.tx_cur;
        next = (slot + 1) % E1K2_TX_DESCS;

        /*
         * TDH == TDT means "no work" to the MAC, so software may own at
         * most N-1 descriptors: the tail must never be advanced onto a
         * descriptor the hardware has not finished with.
         */
        if (!e2k_tx_slot_busy(slot) && !e2k_tx_slot_busy(next))
            break;

        /*
         * Ring full.  The stack has no transmit queue of its own, so a
         * drop here costs TCP a retransmit timeout -- worth waiting for the
         * oldest frame to retire, which takes at most one frame time.  But
         * never with interrupts masked: wait outside the lock, and only if
         * the caller had interrupts on.  From interrupt context (an ARP or
         * ICMP reply sent from the receive handler) drop at once.  A
         * transmitter that has stopped altogether is the watchdog's
         * business, not this loop's.
         */
        spinlock_release_irq(&e2k_tx_lock, flags);
        if (!(flags & 0x200ul) || waited_us >= E1K2_TX_WAIT_US) {
            e2k.netdev.tx_dropped++;
            return -ENOBUFS;
        }
        timer_busywait_us(E1K2_TX_POLL_US);
        waited_us += E1K2_TX_POLL_US;
    }

    uint8_t *buf = e2k.tx_buf + slot * E1K2_BUF_SIZE;
    memcpy(buf, frame, len);

    /* The MAC pads short frames (TCTL.PSP) only from 17 bytes up; pad in
     * software to the 60-byte minimum so nothing shorter reaches it. */
    uint32_t xlen = (uint32_t)len;
    if (xlen < 60) {
        memset(buf + len, 0, 60 - len);
        xlen = 60;
    }

    volatile struct e1k2_tx_desc *d = &e2k.tx_ring[slot];
    d->addr    = (uint64_t)(e2k.tx_buf_phys + slot * E1K2_BUF_SIZE);
    d->length  = (uint16_t)xlen;
    d->cso     = 0;
    d->css     = 0;
    d->special = 0;
    d->status  = 0;
    d->cmd     = TXD_CMD_EOP | TXD_CMD_IFCS | TXD_CMD_RS;

    e2k.tx_cur = next;
    /* The buffer and descriptor must be in memory before the doorbell. */
    e2k_barrier();
    e2k_write(R_TDT, e2k.tx_cur);

    spinlock_release_irq(&e2k_tx_lock, flags);
    return 0;
}

static void e2k_set_allmulti(netdev_t *dev, int on) {
    (void)dev;
    uint32_t rctl = e2k_read(R_RCTL);
    e2k_write(R_RCTL, on ? (rctl | RCTL_MPE) : (rctl & ~RCTL_MPE));
}

static void e2k_write_rar0(const uint8_t mac[6]) {
    e2k_write(R_RAL0, (uint32_t)mac[0] | (uint32_t)mac[1] << 8 |
                      (uint32_t)mac[2] << 16 | (uint32_t)mac[3] << 24);
    e2k_write(R_RAH0, (uint32_t)mac[4] | (uint32_t)mac[5] << 8 | RAH_AV);
}

static int e2k_set_hwaddr(netdev_t *dev, const uint8_t mac[6]) {
    (void)dev;
    e2k_write_rar0(mac);
    return 0;
}

static const struct netdev_ops e2k_ops = {
    .xmit = e2k_xmit,
    .set_allmulti = e2k_set_allmulti,
    .set_hwaddr = e2k_set_hwaddr,
};

/* ----- bring-up ----- */

/* RAR0 as the hardware loaded it; 0 if it holds a valid address. */
static int e2k_read_rar0(uint8_t mac[6]) {
    uint32_t ral = e2k_read(R_RAL0);
    uint32_t rah = e2k_read(R_RAH0);

    if (!(rah & RAH_AV))
        return -1;
    mac[0] = (uint8_t)ral;
    mac[1] = (uint8_t)(ral >> 8);
    mac[2] = (uint8_t)(ral >> 16);
    mac[3] = (uint8_t)(ral >> 24);
    mac[4] = (uint8_t)rah;
    mac[5] = (uint8_t)(rah >> 8);
    /* All zeros or a multicast address is not a station address. */
    if ((mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) == 0 ||
        (mac[0] & 1))
        return -1;
    return 0;
}

static void *e2k_dma_alloc(size_t bytes, uint32_t *phys) {
    size_t pages = (bytes + 4095) / 4096;
    void *p = pmm_alloc_contiguous(pages);

    if (!p)
        return NULL;
    memset(p, 0, pages * 4096);
    *phys = (uint32_t)(uintptr_t)p - 0xC0000000u;
    return p;
}

/* Program the TX ring registers for our (empty) ring. */
static void e2k_setup_tx_ring(void) {
    e2k_write(R_TDBAL, e2k.tx_ring_phys);
    e2k_write(R_TDBAH, 0);
    e2k_write(R_TDLEN, E1K2_TX_DESCS * sizeof(struct e1k2_tx_desc));
    e2k_write(R_TDH, 0);
    e2k_write(R_TDT, 0);
    e2k.tx_cur = 0;
}

/*
 * I219 hangs in reset if the descriptor rings were left in a state the
 * hardware flags in PCI config 0xE4 (a PXE boot, or an OS that did not shut
 * the port down cleanly).  Linux's e1000_flush_desc_rings(): push one dummy
 * transmit through, and if that is not enough, cycle the receiver with its
 * prefetch thresholds opened up.  Done on our own rings, set up just before.
 */
static void e2k_flush_desc_rings(pci_device_t *pdev) {
    uint16_t hang;
    uint32_t rctl, rxdctl;

    e2k_write(R_FEXTNVM11, e2k_read(R_FEXTNVM11) | FEXTNVM11_DISABLE_MULR_FIX);
    hang = pci_read_config16(pdev->bus, pdev->slot, pdev->func,
                             PCICFG_DESC_RING_STATUS);
    if (!(hang & FLUSH_DESC_REQUIRED))
        return;

    kprint("e1000e: descriptor flush required before reset (I219)\n");

    /* TX: one 512-byte dummy frame through our ring. */
    e2k_setup_tx_ring();
    e2k_write(R_TCTL, e2k_read(R_TCTL) | TCTL_EN);
    e2k.tx_ring[0].addr = e2k.tx_buf_phys;
    e2k.tx_ring[0].length = 512;
    e2k.tx_ring[0].cmd = TXD_CMD_IFCS;
    e2k.tx_ring[0].status = 0;
    e2k_write(R_TDT, 1);
    e2k_flush();
    timer_busywait_ms(1);

    hang = pci_read_config16(pdev->bus, pdev->slot, pdev->func,
                             PCICFG_DESC_RING_STATUS);
    if (!(hang & FLUSH_DESC_REQUIRED))
        return;

    /* RX: disable, open the thresholds, pulse the receiver. */
    rctl = e2k_read(R_RCTL);
    e2k_write(R_RCTL, rctl & ~RCTL_EN);
    e2k_flush();
    timer_busywait_ms(1);
    rxdctl = e2k_read(R_RXDCTL);
    rxdctl &= 0xFFFFC000u;
    rxdctl |= 0x1Fu | (1u << 8) | RXDCTL_THRESH_UNIT_DESC;
    e2k_write(R_RXDCTL, rxdctl);
    e2k_write(R_RCTL, rctl | RCTL_EN);
    e2k_flush();
    timer_busywait_ms(1);
    e2k_write(R_RCTL, rctl & ~RCTL_EN);
}

/*
 * Point the receive ring registers at our ring with nothing handed to the
 * hardware (RDH == RDT).  Whatever a pre-boot network stack left there may
 * name buffers in memory the kernel has since reclaimed, and the I219 flush
 * pulses the receiver: it must have nothing of anyone else's to DMA into.
 */
static void e2k_setup_rx_ring_idle(void) {
    e2k_write(R_RDBAL, e2k.rx_ring_phys);
    e2k_write(R_RDBAH, 0);
    e2k_write(R_RDLEN, E1K2_RX_DESCS * sizeof(struct e1k2_rx_desc));
    e2k_write(R_RDH, 0);
    e2k_write(R_RDT, 0);
}

/*
 * Make sure no PCIe transaction is outstanding before the MAC resets: a
 * descriptor fetch or write-back in flight across CTRL.RST loses its
 * completion, after which MMIO reads return all-ones or hang.  Stop new
 * requests (GIO master disable), wait for the outstanding ones to drain,
 * then turn everything off and let it settle.
 */
static void e2k_quiesce(void) {
    unsigned i;

    e2k_write(R_CTRL, e2k_read(R_CTRL) | CTRL_GIO_MASTER_DISABLE);
    for (i = 0; i < E1K2_MASTER_POLLS; i++) {
        if (!(e2k_read(R_STATUS) & STATUS_GIO_MASTER_ENABLE))
            break;
        timer_busywait_us(100);
    }
    if (i == E1K2_MASTER_POLLS)
        kprint("e1000e: bus master requests did not drain; resetting anyway\n");

    e2k_write(R_IMC, 0xFFFFFFFFu);
    e2k_write(R_RCTL, 0);
    e2k_write(R_TCTL, TCTL_PSP);
    e2k_flush();
    timer_busywait_ms(10);
}

/*
 * The full path from whatever state the MAC is in to freshly reset: our
 * rings in the ring registers, the I219 descriptor flush if the hardware
 * asks for one, the bus-master handshake, then CTRL.RST.
 *
 * MAC reset only: on the PCH parts the PHY may belong to the Management
 * Engine (FWSM.RSPCIPHY clear).  The software flag (MDIO ownership on the
 * 82574 -- the same bit) keeps firmware off the shared configuration while
 * the MAC resets.
 */
static void e2k_hw_reset(pci_device_t *pdev) {
    unsigned i;

    e2k_write(R_IMC, 0xFFFFFFFFu);
    e2k_write(R_RCTL, e2k_read(R_RCTL) & ~RCTL_EN);
    e2k_write(R_TCTL, e2k_read(R_TCTL) & ~TCTL_EN);
    e2k_flush();
    timer_busywait_ms(1);

    e2k_setup_rx_ring_idle();
    e2k_setup_tx_ring();
    if (e2k.kind == K_SPT)
        e2k_flush_desc_rings(pdev);

    e2k_quiesce();

    int owned = (e2k_swflag_acquire() == 0);
    e2k_write(R_CTRL, e2k_read(R_CTRL) | CTRL_RST);
    timer_busywait_ms(E1K2_RESET_MS);
    if (owned)
        e2k_swflag_release();

    if (e2k.kind == K_82574) {
        /* The NVM reload (EECD.AUTO_RD), then the NVM-driven PHY
         * configuration, must finish before the NVM or PHY is touched. */
        for (i = 0; i < 10; i++) {
            if (e2k_read(R_EECD) & EECD_AUTO_RD)
                break;
            timer_busywait_ms(1);
        }
        timer_busywait_ms(25);
    }

    e2k_write(R_IMC, 0xFFFFFFFFu);
    (void)e2k_read(R_ICR);
}

static void e2k_irq_remove(pci_device_t *pdev);

static void e2k_dma_free(volatile void *p, size_t bytes) {
    if (p)
        pmm_free_contiguous((void *)(uintptr_t)p, (bytes + 4095) / 4096);
}

/*
 * Undo a partial attach: no DMA may outlive the memory it targets, and
 * manageability firmware must get the port back.
 */
static void e2k_teardown(pci_device_t *pdev) {
    if (e2k.mmio) {
        e2k_write(R_IMC, 0xFFFFFFFFu);
        e2k_write(R_RCTL, e2k_read(R_RCTL) & ~RCTL_EN);
        e2k_write(R_TCTL, e2k_read(R_TCTL) & ~TCTL_EN);
        e2k_write(R_CTRL_EXT, e2k_read(R_CTRL_EXT) & ~CTRL_EXT_DRV_LOAD);
        e2k_flush();
    }
    e2k_irq_remove(pdev);
    uint16_t cmd = pci_read_config16(pdev->bus, pdev->slot, pdev->func,
                                     PCI_CONFIG_COMMAND);
    pci_write_config16(pdev->bus, pdev->slot, pdev->func, PCI_CONFIG_COMMAND,
                       (uint16_t)(cmd & ~PCI_COMMAND_MASTER));
    timer_busywait_ms(1);
    e2k_dma_free(e2k.rx_ring, E1K2_RX_DESCS * sizeof(struct e1k2_rx_desc));
    e2k_dma_free(e2k.tx_ring, E1K2_TX_DESCS * sizeof(struct e1k2_tx_desc));
    e2k_dma_free(e2k.rx_buf, E1K2_RX_DESCS * E1K2_BUF_SIZE);
    e2k_dma_free(e2k.tx_buf, E1K2_TX_DESCS * E1K2_BUF_SIZE);
    e2k.rx_ring = NULL;
    e2k.tx_ring = NULL;
    e2k.rx_buf = NULL;
    e2k.tx_buf = NULL;
}

/* ----- interrupt setup (same ladder as r8168) ----- */

static void e2k_legacy_irq_fixup(void);

static int e2k_irq_install(pci_device_t *pdev, int kind) {
    int irq;

    switch (kind) {
    case IRQ_MSI:
        if (pci_find_capability(pdev, PCI_CAP_ID_MSI) == 0)
            return -1;
        irq = irq_alloc_vector();
        break;
    case IRQ_LINE:
        irq = pci_get_irq(pdev);
        /* INTx Disable survives from firmware, or from an earlier routed
         * attempt's teardown; the pin never asserts while it is set. */
        if (irq >= 0)
            pci_intx_enable(pdev, 1);
        break;
    case IRQ_ROUTED:
        irq = pci_route_intx(pdev);
        break;
    default:
        return -1;
    }
    if (irq < 0)
        return -1;
    if (kind != IRQ_MSI)
        e2k_legacy_irq_fixup();

    e2k.irq_kind = kind;     /* the handler's INT_ASSERTED test needs it */
    if (request_irq((unsigned int)irq, e2k_irq, IRQF_SHARED, "e1000e", &e2k) != 0) {
        kprintf("e1000e: could not install IRQ %d handler\n", irq);
        if (kind == IRQ_MSI) {
            irq_free_vector(irq);
        } else if (kind == IRQ_ROUTED) {
            pci_unroute_intx(pdev, irq);
            irq_free_vector(irq);
        }
        e2k.irq_kind = IRQ_NONE_K;
        return -1;
    }
    if (kind == IRQ_MSI && pci_enable_msi(pdev, (uint8_t)irq) != 0) {
        free_irq((unsigned int)irq, &e2k);
        irq_free_vector(irq);
        e2k.irq_kind = IRQ_NONE_K;
        return -1;
    }
    e2k.irq = irq;
    return 0;
}

static void e2k_irq_remove(pci_device_t *pdev) {
    if (e2k.irq_kind == IRQ_NONE_K)
        return;
    e2k_write(R_IMC, 0xFFFFFFFFu);
    if (e2k.irq_kind == IRQ_MSI)
        (void)pci_disable_msi(pdev);
    free_irq((unsigned int)e2k.irq, &e2k);
    if (e2k.irq_kind == IRQ_MSI) {
        irq_free_vector(e2k.irq);
    } else if (e2k.irq_kind == IRQ_ROUTED) {
        pci_unroute_intx(pdev, e2k.irq);
        irq_free_vector(e2k.irq);
    }
    e2k.irq = -1;
    e2k.irq_kind = IRQ_NONE_K;
}

/*
 * Raise a link-status-change cause through ICS and see whether it lands.
 * On INTx the device is deliberately asserting during the wait, so any
 * foreign interrupt on a shared or misrouted vector would find
 * INT_ASSERTED and look like proof.  The proof is therefore three-sided:
 * the vector is quiet with everything masked, an invocation with LSC in
 * ICR follows the ICS write, and the vector is quiet again once the cause
 * is read and masked.
 */
static int e2k_irq_quiet(void) {
    uint32_t before = e2k.intr_calls;

    timer_busywait_ms(E1K2_IRQ_QUIET_MS);
    return e2k.intr_calls == before;
}

static int e2k_irq_proven(void) {
    uint32_t before;
    unsigned ms;

    e2k_write(R_IMC, 0xFFFFFFFFu);
    (void)e2k_read(R_ICR);
    if (!e2k_irq_quiet())
        return 0;                    /* something else is driving it */

    before = e2k.lsc_count;
    e2k_write(R_IMS, ICR_LSC);
    e2k_write(R_ICS, ICR_LSC);
    for (ms = 0; ms < E1K2_IRQ_PROBE_MS && e2k.lsc_count == before; ms++)
        timer_busywait_ms(1);
    e2k_write(R_IMC, 0xFFFFFFFFu);
    (void)e2k_read(R_ICR);
    if (e2k.lsc_count == before)
        return 0;
    return e2k_irq_quiet();
}

/*
 * From Sunrise Point on, INTx assert and deassert travel as IOSF sideband
 * messages, which clock gating can lose while the port idles: a lost
 * assert leaves RXT0 latched with no interrupt and receive stops for good;
 * a lost deassert storms the line.  Legacy-interrupt mode must keep that
 * clock running.  MSI needs none of this.
 */
static void e2k_legacy_irq_fixup(void) {
    if (e2k.kind != K_SPT)
        return;
    e2k_write(R_FEXTNVM7, e2k_read(R_FEXTNVM7) | FEXTNVM7_SIDE_CLK_UNGATE);
    e2k_write(R_FEXTNVM9, e2k_read(R_FEXTNVM9) |
                          FEXTNVM9_IOSFSB_CLKGATE_DIS |
                          FEXTNVM9_IOSFSB_CLKREQ_DIS);
}

/*
 * MSI first (straight to the local APIC; nothing to route, and UEFI leaves
 * Interrupt Line at 0xFF), then the firmware line, then pci_route_intx().
 * Each is proven through ICS.  If none can be, the first that installs is
 * kept with a warning rather than refusing an interrupt that may work.
 */
static int e2k_setup_irq(pci_device_t *pdev) {
    static const int kinds[] = { IRQ_MSI, IRQ_LINE, IRQ_ROUTED };
    static const char *const names[] = { "none", "MSI", "IRQ line", "routed INTx" };
    size_t i;

    e2k.irq = -1;
    e2k.irq_kind = IRQ_NONE_K;

    for (i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
        if (e2k_irq_install(pdev, kinds[i]) != 0)
            continue;
        if (e2k_irq_proven()) {
            kprintf("e1000e: %s %d verified\n", names[kinds[i]], e2k.irq);
            return 0;
        }
        kprintf("e1000e: no interrupt on %s %d\n", names[kinds[i]], e2k.irq);
        e2k_irq_remove(pdev);
    }
    for (i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
        if (e2k_irq_install(pdev, kinds[i]) == 0) {
            kprintf("e1000e: WARNING: using unverified %s %d\n",
                    names[kinds[i]], e2k.irq);
            return 0;
        }
    }
    kprint("e1000e: no usable interrupt; receive cannot work\n");
    return -1;
}

static int e2k_setup(pci_device_t *pdev, int kind) {
    uint8_t mac_before[6], mac[6];
    int have_before;
    size_t rx_ring_bytes = E1K2_RX_DESCS * sizeof(struct e1k2_rx_desc);
    size_t tx_ring_bytes = E1K2_TX_DESCS * sizeof(struct e1k2_tx_desc);

    if (e2k.registered) {
        kprint("e1000e: additional controller ignored (single instance)\n");
        return -1;
    }
    e2k.pdev = pdev;
    e2k.kind = kind;

    /* Memory space + bus mastering. */
    uint16_t cmd = pci_read_config16(pdev->bus, pdev->slot, pdev->func,
                                     PCI_CONFIG_COMMAND);
    pci_write_config16(pdev->bus, pdev->slot, pdev->func, PCI_CONFIG_COMMAND,
                       cmd | 0x0002 | 0x0004);

    e2k.mmio = pci_iomap(pdev, 0, 0x20000);
    if (!e2k.mmio) {
        kprint("e1000e: could not map BAR0\n");
        goto fail;
    }

    /* The address firmware left in RAR0, in case reset does not reload it. */
    have_before = (e2k_read_rar0(mac_before) == 0);

    /* DMA memory first: the reset path points the hardware at our rings
     * before anything is enabled. */
    e2k.rx_ring = e2k_dma_alloc(rx_ring_bytes, &e2k.rx_ring_phys);
    e2k.tx_ring = e2k_dma_alloc(tx_ring_bytes, &e2k.tx_ring_phys);
    e2k.rx_buf = e2k_dma_alloc(E1K2_RX_DESCS * E1K2_BUF_SIZE, &e2k.rx_buf_phys);
    e2k.tx_buf = e2k_dma_alloc(E1K2_TX_DESCS * E1K2_BUF_SIZE, &e2k.tx_buf_phys);
    if (!e2k.rx_ring || !e2k.tx_ring || !e2k.rx_buf || !e2k.tx_buf) {
        kprint("e1000e: DMA allocation failed\n");
        goto fail;
    }

    if (e2k_is_pch() && !(e2k_read(R_FWSM) & FWSM_RSPCIPHY))
        kprint("e1000e: PHY reset blocked by firmware\n");
    e2k_hw_reset(pdev);

    /* Link: let the PHY autonegotiate; the MAC follows its result. */
    uint32_t ctrl = e2k_read(R_CTRL);
    ctrl |= CTRL_SLU | CTRL_ASDE;
    ctrl &= ~(CTRL_LRST | CTRL_FRCSPD | CTRL_FRCDPLX);
    e2k_write(R_CTRL, ctrl);

    /* A driver owns the port now (manageability firmware backs off), and
     * the bit Intel documents as required on these families. */
    e2k_write(R_CTRL_EXT, e2k_read(R_CTRL_EXT) | CTRL_EXT_DRV_LOAD |
                          CTRL_EXT_BIT22);
    if (e2k_is_pch())
        e2k_write(R_KABGTXD, e2k_read(R_KABGTXD) | KABGTXD_BGSQLBIAS);

    /* Station address: the reloaded RAR0, else what firmware had there. */
    if (e2k_read_rar0(mac) != 0) {
        if (!have_before) {
            kprint("e1000e: no valid MAC address in RAR0; not attaching\n");
            goto fail;
        }
        memcpy(mac, mac_before, 6);
    }
    memcpy(e2k.netdev.hwaddr, mac, 6);
    e2k_write_rar0(mac);

    for (int i = 0; i < 128; i++)
        e2k_write(R_MTA + i * 4, 0);

    e2k_phy_bringup();

    /* Rings. */
    memset((void *)(uintptr_t)e2k.rx_ring, 0, rx_ring_bytes);
    memset((void *)(uintptr_t)e2k.tx_ring, 0, tx_ring_bytes);
    for (int i = 0; i < E1K2_RX_DESCS; i++)
        e2k.rx_ring[i].addr =
            (uint64_t)(e2k.rx_buf_phys + (uint32_t)i * E1K2_BUF_SIZE);

    e2k_write(R_RFCTL, e2k_read(R_RFCTL) & ~RFCTL_EXSTEN);   /* legacy RX */
    e2k_write(R_RDBAL, e2k.rx_ring_phys);
    e2k_write(R_RDBAH, 0);
    e2k_write(R_RDLEN, (uint32_t)rx_ring_bytes);
    e2k_write(R_RDH, 0);
    e2k_write(R_RDT, E1K2_RX_DESCS - 1);
    e2k.rx_cur = 0;

    e2k_setup_tx_ring();
    e2k_write(R_TXDCTL, (e2k_read(R_TXDCTL) & ~(0x3Fu << 16)) |
                        TXDCTL_WTHRESH_1 | TXDCTL_GRAN | TXDCTL_BIT22);

    /* Copper defaults: IPGT 8, IPGR1 8, IPGR2 6. */
    e2k_write(R_TIPG, 8 | (8 << 10) | (6 << 20));
    e2k_write(R_TCTL, TCTL_EN | TCTL_PSP | TCTL_RTLC |
                      (0x0F << TCTL_CT_SHIFT) | (0x3F << TCTL_COLD_SHIFT));
    e2k_write(R_RCTL, RCTL_EN | RCTL_BAM | RCTL_BSIZE_2048 | RCTL_SECRC);

    if (e2k_setup_irq(pdev) != 0)
        goto fail;

    strlcpy(e2k.netdev.name, "eth0", NETDEV_NAME_MAX);
    e2k.netdev.mtu = 1500;
    e2k.netdev.flags = NETDEV_IFF_UP | NETDEV_IFF_BROADCAST |
                       NETDEV_IFF_RUNNING | NETDEV_IFF_MULTICAST;
    e2k.netdev.ops = &e2k_ops;
    e2k.netdev.driver_data = &e2k;
    netdev_register(&e2k.netdev);
    e2k.registered = 1;

    kprintf("e1000e: %s %04x %02x:%02x:%02x:%02x:%02x:%02x %s %d%s\n",
            kind == K_82574 ? "82574" : kind == K_LPT ? "I217/I218" : "I219",
            (unsigned)pdev->device_id,
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
            e2k.irq_kind == IRQ_MSI ? "msi" : "irq", e2k.irq,
            (e2k_read(R_FWSM) & FWSM_FW_VALID) ? ", manageability firmware present" : "");

    /* Sample the link while LSC is still masked, so the handler and this
     * cannot race over link_status. */
    e2k.link_status = ~0u;
    e2k_report_link();

    /* Only now unmask, so nothing arrives before the netdev exists.  The
     * probe's ICR reads may have swallowed RXT0 for frames that arrived
     * during it; re-raise it so those are collected now rather than when
     * the next frame comes. */
    e2k_write(R_ITR, E1K2_ITR);
    e2k_write(R_IMS, E1K2_IMS);
    e2k_write(R_ICS, ICR_RXT0);
    return 0;

fail:
    e2k_teardown(pdev);
    e2k.mmio = NULL;
    return -1;
}

/*
 * driver_data is the chip class.  I217/I218 (Lynx Point, Wildcat Point) and
 * I219 (Sunrise Point onwards) IDs from Linux's e1000e device table.
 */
static const device_id_t e2k_ids[] = {
    { E1K2_VENDOR, 0x10D3, 0, 0, K_82574 },   /* 82574L - qemu `e1000e` */
    { E1K2_VENDOR, 0x10F6, 0, 0, K_82574 },   /* 82574LA */
    { E1K2_VENDOR, 0x150C, 0, 0, K_82574 },   /* 82583V */
    { E1K2_VENDOR, 0x153A, 0, 0, K_LPT },     /* I217-LM */
    { E1K2_VENDOR, 0x153B, 0, 0, K_LPT },     /* I217-V */
    { E1K2_VENDOR, 0x155A, 0, 0, K_LPT },     /* I218-LM */
    { E1K2_VENDOR, 0x1559, 0, 0, K_LPT },     /* I218-V */
    { E1K2_VENDOR, 0x15A0, 0, 0, K_LPT },     /* I218-LM2 */
    { E1K2_VENDOR, 0x15A1, 0, 0, K_LPT },     /* I218-V2 */
    { E1K2_VENDOR, 0x15A2, 0, 0, K_LPT },     /* I218-LM3 */
    { E1K2_VENDOR, 0x15A3, 0, 0, K_LPT },     /* I218-V3 */
    { E1K2_VENDOR, 0x156F, 0, 0, K_SPT },     /* I219-LM */
    { E1K2_VENDOR, 0x1570, 0, 0, K_SPT },     /* I219-V */
    { E1K2_VENDOR, 0x15B7, 0, 0, K_SPT },     /* I219-LM2 */
    { E1K2_VENDOR, 0x15B8, 0, 0, K_SPT },     /* I219-V2 */
    { E1K2_VENDOR, 0x15B9, 0, 0, K_SPT },     /* I219-LM3 */
    { E1K2_VENDOR, 0x15D7, 0, 0, K_SPT },     /* I219-LM4 */
    { E1K2_VENDOR, 0x15D8, 0, 0, K_SPT },     /* I219-V4 */
    { E1K2_VENDOR, 0x15E3, 0, 0, K_SPT },     /* I219-LM5 */
    { E1K2_VENDOR, 0x15D6, 0, 0, K_SPT },     /* I219-V5 */
    { E1K2_VENDOR, 0x15BD, 0, 0, K_SPT },     /* I219-LM6 */
    { E1K2_VENDOR, 0x15BE, 0, 0, K_SPT },     /* I219-V6 */
    { E1K2_VENDOR, 0x15BB, 0, 0, K_SPT },     /* I219-LM7 */
    { E1K2_VENDOR, 0x15BC, 0, 0, K_SPT },     /* I219-V7 */
    { E1K2_VENDOR, 0x15DF, 0, 0, K_SPT },     /* I219-LM8 */
    { E1K2_VENDOR, 0x15E0, 0, 0, K_SPT },     /* I219-V8 */
    { E1K2_VENDOR, 0x15E1, 0, 0, K_SPT },     /* I219-LM9 */
    { E1K2_VENDOR, 0x15E2, 0, 0, K_SPT },     /* I219-V9 */
    { E1K2_VENDOR, 0x0D4E, 0, 0, K_SPT },     /* I219-LM10 */
    { E1K2_VENDOR, 0x0D4F, 0, 0, K_SPT },     /* I219-V10 */
    { E1K2_VENDOR, 0x0D4C, 0, 0, K_SPT },     /* I219-LM11 */
    { E1K2_VENDOR, 0x0D4D, 0, 0, K_SPT },     /* I219-V11 */
    { E1K2_VENDOR, 0x0D53, 0, 0, K_SPT },     /* I219-LM12 */
    { E1K2_VENDOR, 0x0D55, 0, 0, K_SPT },     /* I219-V12 */
    { 0, 0, 0, 0, 0 },
};

static int e2k_pci_attach(struct device *dev) {
    pci_device_t *pdev = pci_find_device_by_kdev(dev);
    const device_id_t *id;

    if (!pdev) return -1;
    for (id = e2k_ids; id->vendor_id != 0; id++) {
        if (id->vendor_id == pdev->vendor_id && id->device_id == pdev->device_id)
            return e2k_setup(pdev, (int)id->driver_data);
    }
    return -1;
}

static int e2k_pci_detach(struct device *dev) { (void)dev; return 0; }

static struct driver e2k_pci_driver = {
    .name = "e1000e-pci",
    .id_table = e2k_ids,
    .attach = e2k_pci_attach,
    .detach = e2k_pci_detach,
};

void e1000e_init(void) {
    static int registered;
    if (!registered) {
        (void)driver_register(&e2k_pci_driver, &pci_bus_type);
        registered = 1;
    }
}
