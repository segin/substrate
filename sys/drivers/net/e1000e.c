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
 *     arbitrates the MDIO bus with firmware, with page selection for the
 *     PCH PHYs.  At bring-up the PHY is taken out of Ultra Low Power mode
 *     and forced SMBus mode (with a LANPHYPC power cycle if it still does
 *     not answer), cleared of LPLU and Gigabit-disable, and set to
 *     advertise every speed and autonegotiate.
 *
 * Not covered: the older PCH PHYs (82577/82578 on Ibex Peak, 82579 on
 * Cougar Point / Panther Point), which need per-generation PHY workarounds.
 *
 * QEMU: -netdev user,id=n0 -device e1000e,netdev=n0
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <machine/intr.h>
#include <machine/pmm.h>
#include <machine/vmparam.h>
#include <kern/console.h>
#include <kern/driver.h>
#include <kern/pci.h>
#include <kern/time.h>
#include <kern/sched.h>
#include <sys/irq.h>
#include <sys/kthread.h>
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

#define R_TARC0             0x3840   /* transmit arbitration, queue 0 */
#define R_TARC1             0x3940
#define R_IOSFPC            0x0F28
#define R_PBECCSTS          0x100C   /* packet buffer ECC */
#define R_GCR               0x5B00
#define R_GCR2              0x5B64
#define R_MANC              0x5820
#define R_MANC2H            0x5860
#define R_FACTPS            0x5B30

/* TARC0/TARC1 required settings. */
#define TARC0_82574_CLEAR   0x78000000u   /* bits 30:27 */
#define TARC0_82574_SET     0x04000000u   /* bit 26 */
#define TARC0_PCH_SET       0x0D800000u   /* bits 23, 24, 26, 27 */
#define TARC1_PCH_SET       0x45000000u   /* bits 24, 26, 30 */
#define TARC1_MULR_INV      0x10000000u   /* bit 28 = !TCTL.MULR */
#define TARC0_CB_MULTIQ_MASK  0x30000000u
#define TARC0_CB_MULTIQ_2_REQ 0x20000000u
#define IOSFPC_RCTL_RDMTS_HEX 0x00010000u

#define PBECCSTS_ECC_ENABLE 0x00010000u
#define GCR_NO_SNOOP_ALL    0x0000003Fu
#define GCR_BIT22           0x00400000u
#define GCR2_BIT0           0x00000001u

/* Manageability. */
#define MANC_ARP_EN         0x00002000u
#define MANC_RCV_TCO_EN     0x00020000u
#define MANC_EN_MNG2HOST    0x00200000u
#define MANC2H_PORT_623     0x00000020u
#define MANC2H_PORT_664     0x00000040u
#define FACTPS_MNGCG        0x20000000u
#define FWSM_MODE_MASK      0x0000000Eu
#define FWSM_MODE_SHIFT     1
#define FWSM_MODE_PT        2           /* pass-through */
#define FWSM_WLOCK_MAC_MASK 0x00000380u
#define FWSM_WLOCK_MAC_SHIFT 7

/* Receive-address entries beyond RAR0 (82574: 15 in all; PCH: 11 shared). */
#define E1K2_RAR_82574      14
#define E1K2_RAR_PCH        11

#define R_FEXTNVM6          0x0010
#define R_FEXTNVM4          0x0024
#define R_KMRNCTRLSTA       0x0034   /* Kumeran control/status */
#define R_SVCR              0x00F0
#define R_SVT               0x00F4
#define R_LTRV              0x00F8
#define R_PBA               0x1000
#define R_PCIEANACFG        0x0F18

#define FEXTNVM4_BEACON_MASK 0x00000007u
#define FEXTNVM4_BEACON_8US 0x00000007u
#define FEXTNVM6_REQ_PLL_CLK 0x00000100u
#define FEXTNVM6_K1_ENTRY_CONDITION 0x00000200u
#define FEXTNVM6_K1_OFF_ENABLE 0x80000000u

#define KMRN_OFFSET_SHIFT   16
#define KMRN_OFFSET_MASK    0x001F0000u
#define KMRN_REN            0x00200000u
#define KMRN_K1_CONFIG      0x07
#define KMRN_K1_ENABLE      0x0002

#define TIPG_IPGT_MASK      0x000003FFu
#define PBA_RXA_MASK        0x0000FFFFu   /* receive allocation, KB */

/* LTRV: snoop latency in 15:0, no-snoop in 31:16, each value[9:0] and
 * scale[12:10] (value x 32^scale ns) plus a requirement bit. */
#define LTRV_VALUE_MASK     0x03FF
#define LTRV_SCALE_SHIFT    10
#define LTRV_SCALE_MASK     0x1C00
#define LTRV_SCALE_MAX      5
#define LTRV_SNOOP_REQ      (1u << 15)
#define LTRV_NOSNOOP_SHIFT  16
#define LTRV_NOSNOOP_REQ    (1u << 31)
#define LTRV_SEND           (1u << 30)
#define SVT_OFF_HWM_MASK    0x0000001Fu
#define SVCR_OFF_EN         0x00000001u
#define SVCR_OFF_MASKINT    0x00001000u
#define PCI_LTR_CAP_LPT     0xA8          /* platform max snoop/no-snoop */
#define E1K2_MAX_FRAME_LTR  1522          /* max frame incl. VLAN tag + FCS */

#define R_FEXTNVM           0x0028
#define R_FEXTNVM3          0x003C
#define R_PHY_CTRL          0x0F10   /* MAC-side PHY power control */
#define R_H2ME              0x5B50   /* host to manageability engine */

/* FEXTNVM: the NVM wants software to configure the PHY after reset. */
#define FEXTNVM_SW_CONFIG   0x08000000u

/* FEXTNVM3: PHY configuration counter (time the PHY gets after power-up). */
#define FEXTNVM3_PHY_CFG_COUNTER_MASK 0x0C000000u
#define FEXTNVM3_PHY_CFG_COUNTER_50MS 0x08000000u

/* PHY_CTRL. */
#define PHY_CTRL_D0A_LPLU   0x00000002u
#define PHY_CTRL_GBE_DISABLE 0x00000040u

/* H2ME. */
#define H2ME_ULP            0x00000800u
#define H2ME_ENFORCE_SETTINGS 0x00001000u

/* FEXTNVM7 / FEXTNVM9 (Sunrise Point and later). */
#define FEXTNVM7_DISABLE_SMB_PERST    0x00000020u
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
#define CTRL_LANPHYPC_OVERRIDE (1u << 16)
#define CTRL_LANPHYPC_VALUE (1u << 17)
#define CTRL_MEHE           (1u << 19)   /* memory error handling (PCH) */
#define CTRL_RST            (1u << 26)
#define CTRL_BIT29          (1u << 29)   /* must be clear on the 82574 */
#define CTRL_PHY_RST        (1u << 31)

/* STATUS bits. */
#define STATUS_FD           (1u << 0)
#define STATUS_LU           (1u << 1)
#define STATUS_SPEED_SHIFT  6
#define STATUS_SPEED_MASK   (3u << STATUS_SPEED_SHIFT)
#define STATUS_GIO_MASTER_ENABLE (1u << 19)

/* EECD (82574). */
#define EECD_AUTO_RD        (1u << 9)    /* NVM auto-read done */

/* CTRL_EXT bits. */
#define CTRL_EXT_LPCD       (1u << 2)    /* LANPHYPC power cycle done */
#define CTRL_EXT_FORCE_SMBUS (1u << 11)
#define CTRL_EXT_RO_DIS     (1u << 17)   /* relaxed ordering disable */
#define CTRL_EXT_BIT22      (1u << 22)   /* required set on 82571+/ICH */
#define CTRL_EXT_BIT23      (1u << 23)   /* must be clear on the 82574 */
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
#define FWSM_ULP_CFG_DONE   0x00000400u
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
#define RFCTL_NFSW_DIS      (1u << 6)    /* NFS write filtering off */
#define RFCTL_NFSR_DIS      (1u << 7)    /* NFS read filtering off */
#define RFCTL_EXSTEN        (1u << 15)   /* extended RX status (descriptors) */

/* TCTL bits. */
#define TCTL_EN             (1u << 1)
#define TCTL_PSP            (1u << 3)    /* pad short packets */
#define TCTL_CT_SHIFT       4            /* collision threshold */
#define TCTL_COLD_SHIFT     12           /* collision distance */
#define TCTL_RTLC           (1u << 24)   /* retransmit on late collision */
#define TCTL_MULR           (1u << 28)   /* multiple request support */

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
#define PHY_ANAR            0x04
#define PHY_CTRL1000        0x09
#define BMCR_ANRESTART      0x0200
#define BMCR_ISOLATE        0x0400
#define BMCR_PDOWN          0x0800
#define BMCR_ANENABLE       0x1000
#define BMCR_LOOPBACK       0x4000
#define BMCR_RESET          0x8000
#define ANAR_ADVERTISE      0x01E1   /* 10/100 half and full, IEEE 802.3 */
#define CTRL1000_FD         0x0200   /* 1000BASE-T full duplex */

/*
 * PCH PHY registers are addressed by page and register: PHY_REG() packs
 * both, and e2k_phy_rw_locked() unpacks them into page select plus MDIO
 * address.
 */
#define PHY_REG(page, reg)  (((uint32_t)(page) << 5) | ((reg) & 0x1F))
#define PHY_PAGE_SELECT     0x1F
#define PHY_MULTI_PAGE_MAX  0x0F

#define I82577_CFG          22       /* page 0 */
#define I82577_CFG_DOWNSHIFT    0x0C00
#define I82577_CFG_CRS_ON_TX    0x8000
#define I82577_PHY_CTRL2    18       /* page 0 */
#define I82577_CTRL2_MDIX_MASK  0x0600
#define I82577_CTRL2_MDIX_AUTO  0x0400

#define HV_OEM_BITS         PHY_REG(768, 25)
#define HV_OEM_LPLU             0x0004
#define HV_OEM_GBE_DIS          0x0040
#define HV_OEM_RESTART_AN       0x0400
#define CV_SMB_CTRL         PHY_REG(769, 23)
#define CV_SMB_FORCE_SMBUS      0x0001
#define HV_PM_CTRL          PHY_REG(770, 17)
#define HV_PM_K1_CLK_REQ        0x0200
#define HV_PM_K1_ENABLE         0x4000
#define I217_INBAND_CTRL    PHY_REG(770, 18)
#define I217_INBAND_TX_TIMEOUT_MASK  0x3F00
#define I217_INBAND_TX_TIMEOUT_SHIFT 8
#define I217_PLL_CLOCK_GATE PHY_REG(772, 28)
#define I217_PLL_CLOCK_GATE_MASK 0x07FF
#define I219_PTR_GAP        PHY_REG(776, 20)
#define I82579_EMI_ADDR     0x10     /* page 0: extended indirect access */
#define I82579_EMI_DATA     0x11
#define I217_RX_CONFIG      0xB20C   /* EMI: PHY receive latency */
#define I218_ULP_CONFIG1    PHY_REG(779, 16)
#define ULP_CONFIG1_START                   0x0001
#define ULP_CONFIG1_IND                     0x0004
#define ULP_CONFIG1_STICKY_ULP              0x0010
#define ULP_CONFIG1_INBAND_EXIT             0x0020
#define ULP_CONFIG1_WOL_HOST                0x0040
#define ULP_CONFIG1_RESET_TO_SMBUS          0x0100
#define ULP_CONFIG1_EN_ULP_LANPHYPC         0x0400
#define ULP_CONFIG1_DIS_CLR_STICKY_ON_PERST 0x0800
#define ULP_CONFIG1_DISABLE_SMB_PERST       0x1000

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
#define E1K2_WD_MS          1000         /* worker period */
#define E1K2_TX_HANG_S      3            /* TDH stuck behind TDT this long */
#define E1K2_SWFLAG_MS      1000
#define E1K2_MDIC_MS        20
#define E1K2_IRQ_PROBE_MS   50
#define E1K2_IRQ_QUIET_MS   10

/* Chip classes (driver_data in the ID table). */
#define K_82574             1   /* 82574L / 82583V: discrete, PHY at 1 */
#define K_LPT               2   /* I217 / I218: PCH MAC, PHY at 2 */
#define K_SPT               3   /* I219: as LPT, plus the reset hang */
#define K_MASK              0x0F

/* Per-ID flags, ORed into driver_data above the chip class. */
#define F_ULP               0x10   /* PHY has Ultra Low Power mode */
#define F_CNP               0x20   /* Cannon Point or later: slower ULP exit */
#define F_SPT_ERRATA        0x40   /* Sunrise/Kaby Point transmit errata */
#define F_LPT_LP            0x80   /* I218 on a low-power platform: K1 erratum */

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
    int                   flags;         /* F_* for this device ID */
    uint16_t              phy_rev;       /* PHY ID2 revision field */
    volatile int          link_event;    /* LSC seen: worker runs fixups */
    volatile int          resetting;     /* recovery reset in progress */
    int                   wd_chan;       /* worker wait channel */
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

/* Firmware does not hold back a PHY reset (FWSM.RSPCIPHY); always so off
 * the PCH. */
static int e2k_phy_reset_allowed(void) {
    return !e2k_is_pch() || (e2k_read(R_FWSM) & FWSM_RSPCIPHY);
}

/*
 * One MDIC transaction at MDIO address `addr`; caller holds the software
 * flag.  The completed MDIC echoes the register number: a mismatch means
 * the transaction was not ours or did not happen, and its data is junk.
 */
static int e2k_mdic(int addr, uint32_t op, int reg, uint16_t wdata,
                    uint16_t *rdata) {
    uint32_t v;
    unsigned us;

    e2k_write(R_MDIC, op | ((uint32_t)reg << MDIC_REG_SHIFT) |
                      ((uint32_t)addr << MDIC_PHY_SHIFT) | wdata);
    for (us = 0; ; us += 20) {
        v = e2k_read(R_MDIC);
        if (v & MDIC_READY)
            break;
        if (us >= E1K2_MDIC_MS * 1000)
            return -1;
        timer_busywait_us(20);
    }
    if (v & MDIC_ERROR)
        return -1;
    if (((v >> MDIC_REG_SHIFT) & 0x1F) != (uint32_t)reg)
        return -1;
    if (rdata)
        *rdata = (uint16_t)v;
    return 0;
}

/*
 * PHY register access by PHY_REG(page, reg); caller holds the software
 * flag.  On the PCH PHYs, pages from 768 up live at MDIO address 1 and the
 * rest at 2, and any register above 15 needs the page selected first
 * (through address 1) -- page 0 included.  The 82574 is only ever asked
 * for page 0's standard registers.
 */
static int e2k_phy_rw_locked(int write, uint32_t preg, uint16_t *val) {
    int page = (int)(preg >> 5), reg = (int)(preg & 0x1F), addr;

    if (e2k.kind == K_82574) {
        addr = 1;
    } else {
        addr = page >= 768 ? 1 : 2;
        if (reg > PHY_MULTI_PAGE_MAX &&
            e2k_mdic(1, MDIC_OP_WRITE, PHY_PAGE_SELECT,
                     (uint16_t)(page << 5), NULL) != 0)
            return -1;
    }
    if (write)
        return e2k_mdic(addr, MDIC_OP_WRITE, reg, *val, NULL);
    return e2k_mdic(addr, MDIC_OP_READ, reg, 0, val);
}

static int e2k_phy_read_locked(uint32_t preg, uint16_t *val) {
    return e2k_phy_rw_locked(0, preg, val);
}

static int e2k_phy_write_locked(uint32_t preg, uint16_t val) {
    return e2k_phy_rw_locked(1, preg, &val);
}

/* The PHY ID, or -1 if the PHY does not answer (0 and all-ones are what a
 * dead or unreachable MDIO bus reads as).  Caller holds the flag. */
static int e2k_phy_id_locked(uint16_t *id1, uint16_t *id2) {
    for (int tries = 0; tries < 2; tries++) {
        if (e2k_phy_read_locked(PHY_REG(0, PHY_ID1), id1) == 0 &&
            e2k_phy_read_locked(PHY_REG(0, PHY_ID2), id2) == 0 &&
            *id1 != 0 && *id1 != 0xFFFF)
            return 0;
    }
    return -1;
}

/*
 * Whether the PCH PHY answers.  With no manageability firmware to own the
 * SMBus side, a PHY that does answer is also taken out of forced SMBus
 * mode, in the PHY and in the MAC: previous software may have left it
 * there, and in SMBus mode the PHY never links over PCIe.  Caller holds
 * the flag.
 */
static int e2k_phy_accessible_locked(void) {
    uint16_t id1, id2, v;

    if (e2k_phy_id_locked(&id1, &id2) != 0)
        return 0;
    if (!(e2k_read(R_FWSM) & FWSM_FW_VALID)) {
        if (e2k_phy_read_locked(CV_SMB_CTRL, &v) == 0 &&
            (v & CV_SMB_FORCE_SMBUS))
            (void)e2k_phy_write_locked(CV_SMB_CTRL,
                                       (uint16_t)(v & ~CV_SMB_FORCE_SMBUS));
        e2k_write(R_CTRL_EXT, e2k_read(R_CTRL_EXT) & ~CTRL_EXT_FORCE_SMBUS);
    }
    return 1;
}

/*
 * Power-cycle the PHY through the LANPHYPC pin: drive it low for a moment,
 * release it, and wait for the PHY to come back (CTRL_EXT.LPCD) and load
 * its configuration.  This also brings it out of SMBus mode.
 */
static void e2k_toggle_lanphypc(void) {
    uint32_t v;

    v = e2k_read(R_FEXTNVM3);
    v = (v & ~FEXTNVM3_PHY_CFG_COUNTER_MASK) | FEXTNVM3_PHY_CFG_COUNTER_50MS;
    e2k_write(R_FEXTNVM3, v);

    v = e2k_read(R_CTRL);
    v |= CTRL_LANPHYPC_OVERRIDE;
    v &= ~CTRL_LANPHYPC_VALUE;
    e2k_write(R_CTRL, v);
    e2k_flush();
    timer_busywait_us(10);
    v &= ~CTRL_LANPHYPC_OVERRIDE;
    e2k_write(R_CTRL, v);
    e2k_flush();

    for (int i = 0; i < 20; i++) {
        if (e2k_read(R_CTRL_EXT) & CTRL_EXT_LPCD)
            break;
        timer_busywait_ms(5);
    }
    timer_busywait_ms(30);
}

/* PHY reset through CTRL.PHY_RST, unless firmware holds it back. */
static void e2k_phy_reset(void) {
    if (!e2k_phy_reset_allowed())
        return;
    if (e2k_swflag_acquire() != 0)
        return;
    e2k_write(R_CTRL, e2k_read(R_CTRL) | CTRL_PHY_RST);
    e2k_flush();
    timer_busywait_us(100);
    e2k_write(R_CTRL, e2k_read(R_CTRL) & ~CTRL_PHY_RST);
    e2k_flush();
    e2k_swflag_release();
    timer_busywait_ms(50);
}

/*
 * Leave Ultra Low Power mode.  A driver that enters ULP on the way to Sx
 * (the usual shutdown path with Wake-on-LAN off) leaves the PHY in a state
 * that survives a platform reset (STICKY_ULP, SMBus release on PERST#
 * disabled): every MDIO access then fails and the link never comes up
 * until all power is removed.  Its prior state is unknown, so it is always
 * exited on the parts that have it.
 *
 * With manageability firmware the request goes to it; without, the driver
 * does it: power-cycle through LANPHYPC, unforce SMBus, re-enable K1
 * (hardware disables it on ULP entry), clear the ULP configuration and
 * commit it, and let SMBus release on PERST# again.  Returns non-zero if
 * the PHY should be reset afterwards.
 */
static int e2k_ulp_disable(void) {
    uint16_t v;
    unsigned i, limit;

    if (!(e2k.flags & F_ULP))
        return 0;

    if (e2k_read(R_FWSM) & FWSM_FW_VALID) {
        uint32_t h2me = e2k_read(R_H2ME);
        e2k_write(R_H2ME, (h2me & ~H2ME_ULP) | H2ME_ENFORCE_SETTINGS);
        limit = (e2k.flags & F_CNP) ? 100 : 30;
        for (i = 0; i < limit && (e2k_read(R_FWSM) & FWSM_ULP_CFG_DONE); i++)
            timer_busywait_ms(10);
        if (i == limit)
            kprint("e1000e: firmware did not confirm ULP exit\n");
        e2k_write(R_H2ME, e2k_read(R_H2ME) & ~H2ME_ENFORCE_SETTINGS);
        return 0;
    }

    if (e2k_swflag_acquire() != 0) {
        kprint("e1000e: cannot leave ULP: MDIO bus held\n");
        return 0;
    }
    e2k_toggle_lanphypc();

    if (e2k_phy_read_locked(CV_SMB_CTRL, &v) != 0) {
        /* The MAC may still be in PCIe mode: force SMBus to reach it. */
        e2k_write(R_CTRL_EXT, e2k_read(R_CTRL_EXT) | CTRL_EXT_FORCE_SMBUS);
        timer_busywait_ms(50);
        if (e2k_phy_read_locked(CV_SMB_CTRL, &v) != 0) {
            e2k_swflag_release();
            kprint("e1000e: PHY unreachable while leaving ULP\n");
            return 1;
        }
    }
    (void)e2k_phy_write_locked(CV_SMB_CTRL, (uint16_t)(v & ~CV_SMB_FORCE_SMBUS));
    e2k_write(R_CTRL_EXT, e2k_read(R_CTRL_EXT) & ~CTRL_EXT_FORCE_SMBUS);

    if (e2k_phy_read_locked(HV_PM_CTRL, &v) == 0)
        (void)e2k_phy_write_locked(HV_PM_CTRL, (uint16_t)(v | HV_PM_K1_ENABLE));

    if (e2k_phy_read_locked(I218_ULP_CONFIG1, &v) == 0) {
        v &= (uint16_t)~(ULP_CONFIG1_IND | ULP_CONFIG1_STICKY_ULP |
                         ULP_CONFIG1_RESET_TO_SMBUS | ULP_CONFIG1_WOL_HOST |
                         ULP_CONFIG1_INBAND_EXIT | ULP_CONFIG1_EN_ULP_LANPHYPC |
                         ULP_CONFIG1_DIS_CLR_STICKY_ON_PERST |
                         ULP_CONFIG1_DISABLE_SMB_PERST);
        (void)e2k_phy_write_locked(I218_ULP_CONFIG1, v);
        (void)e2k_phy_write_locked(I218_ULP_CONFIG1,
                                   (uint16_t)(v | ULP_CONFIG1_START));
    }
    e2k_write(R_FEXTNVM7, e2k_read(R_FEXTNVM7) & ~FEXTNVM7_DISABLE_SMB_PERST);
    e2k_swflag_release();
    return 1;
}

/*
 * Make the PCH PHY reachable: leave ULP, then if it still does not answer
 * try forced SMBus mode, then a LANPHYPC power cycle (if firmware allows a
 * PHY reset), then PCIe mode again.  Reset the PHY afterwards when its
 * state was disturbed.  Returns non-zero if the PHY answers.
 */
static int e2k_pch_phy_init(void) {
    int reset = e2k_ulp_disable();
    int ok;

    if (e2k_swflag_acquire() != 0) {
        kprint("e1000e: MDIO bus held by firmware; PHY left as found\n");
        return 0;
    }
    ok = e2k_phy_accessible_locked();
    if (!ok) {
        /* Let the MAC finish retrying any earlier PHY read first. */
        e2k_write(R_CTRL_EXT, e2k_read(R_CTRL_EXT) | CTRL_EXT_FORCE_SMBUS);
        timer_busywait_ms(50);
        ok = e2k_phy_accessible_locked();
    }
    if (!ok) {
        if (!e2k_phy_reset_allowed()) {
            kprint("e1000e: PHY unreachable and its reset is blocked by "
                   "firmware\n");
        } else {
            e2k_toggle_lanphypc();
            ok = e2k_phy_accessible_locked();
            if (!ok) {
                e2k_write(R_CTRL_EXT,
                          e2k_read(R_CTRL_EXT) & ~CTRL_EXT_FORCE_SMBUS);
                ok = e2k_phy_accessible_locked();
            }
            reset = 1;
        }
    }
    e2k_swflag_release();

    if (ok && reset) {
        if (e2k_read(R_FEXTNVM) & FEXTNVM_SW_CONFIG)
            kprint("e1000e: NVM asks software to configure the PHY after "
                   "reset; that configuration is not applied\n");
        e2k_phy_reset();
    }
    return ok;
}

/*
 * Copper link setup: powered, Gigabit allowed, every speed advertised,
 * autonegotiating.  Best effort: a PHY this driver cannot reach may still
 * be linked up by firmware, so failures are reported, not fatal.  Returns
 * non-zero if the PHY answered.
 */
static int e2k_phy_bringup(void) {
    uint16_t id1 = 0, id2 = 0, bmcr = 0, v;
    int rc;

    if (e2k_is_pch() && !e2k_pch_phy_init()) {
        kprint("e1000e: PHY not responding on MDIO; left as found\n");
        return 0;
    }

    if (e2k_swflag_acquire() != 0) {
        kprint("e1000e: MDIO bus held by firmware; PHY left as found\n");
        return 0;
    }
    rc = e2k_phy_id_locked(&id1, &id2);
    if (rc == 0)
        rc = e2k_phy_read_locked(PHY_REG(0, PHY_BMCR), &bmcr);
    if (rc == 0 && bmcr == 0xFFFF)
        rc = -1;
    if (rc != 0) {
        e2k_swflag_release();
        kprint("e1000e: PHY not responding on MDIO; left as found\n");
        return 0;
    }
    e2k.phy_rev = id2 & 0x000F;
    kprintf("e1000e: PHY id %04x:%04x, BMCR 0x%04x\n",
            (unsigned)id1, (unsigned)id2, (unsigned)bmcr);

    /*
     * Low Power Link Up and Gigabit-disable survive a MAC reset from an
     * earlier Sx or Wake-on-LAN path and cap the link at 100 or 10 Mb/s.
     * The MAC's PHY_CTRL holds them, and on the PCH the PHY's OEM bits
     * mirror it; a restart is needed for the PHY to act on the change.
     */
    e2k_write(R_PHY_CTRL, e2k_read(R_PHY_CTRL) &
                          ~(PHY_CTRL_D0A_LPLU | PHY_CTRL_GBE_DISABLE));
    if (e2k_is_pch() &&
        e2k_phy_read_locked(HV_OEM_BITS, &v) == 0) {
        v &= (uint16_t)~(HV_OEM_LPLU | HV_OEM_GBE_DIS);
        if (e2k_phy_reset_allowed())
            v |= HV_OEM_RESTART_AN;
        (void)e2k_phy_write_locked(HV_OEM_BITS, v);
    }

    /* I217/I219 copper: carrier sense during transmit (half duplex needs
     * it), automatic speed downshift on 2-pair cable, automatic MDI-X. */
    if (e2k_is_pch()) {
        if (e2k_phy_read_locked(PHY_REG(0, I82577_CFG), &v) == 0)
            (void)e2k_phy_write_locked(PHY_REG(0, I82577_CFG),
                                       (uint16_t)(v | I82577_CFG_CRS_ON_TX |
                                                  I82577_CFG_DOWNSHIFT));
        if (e2k_phy_read_locked(PHY_REG(0, I82577_PHY_CTRL2), &v) == 0)
            (void)e2k_phy_write_locked(PHY_REG(0, I82577_PHY_CTRL2),
                                       (uint16_t)((v & ~I82577_CTRL2_MDIX_MASK) |
                                                  I82577_CTRL2_MDIX_AUTO));
    }

    /*
     * Advertise 10/100 half and full and 1000 full, and restart
     * autonegotiation from a validated BMCR with power-down, isolate and
     * loopback cleared: a speed or duplex forced by a previous driver
     * otherwise survives a warm reboot as a duplex mismatch.
     */
    if (e2k_phy_reset_allowed()) {
        (void)e2k_phy_write_locked(PHY_REG(0, PHY_ANAR), ANAR_ADVERTISE);
        (void)e2k_phy_write_locked(PHY_REG(0, PHY_CTRL1000), CTRL1000_FD);
        bmcr = (uint16_t)((bmcr & ~(BMCR_PDOWN | BMCR_ISOLATE |
                                    BMCR_LOOPBACK | BMCR_RESET)) |
                          BMCR_ANENABLE | BMCR_ANRESTART);
        (void)e2k_phy_write_locked(PHY_REG(0, PHY_BMCR), bmcr);
    } else if (bmcr & BMCR_PDOWN) {
        kprint("e1000e: PHY is powered down and firmware owns it\n");
    }
    e2k_swflag_release();
    return 1;
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
        e2k.netdev.flags |= NETDEV_IFF_RUNNING;
        kprintf("e1000e: link up, %u Mb/s %s duplex\n",
                speeds[(st & STATUS_SPEED_MASK) >> STATUS_SPEED_SHIFT],
                (st & STATUS_FD) ? "full" : "half");
    } else {
        e2k.netdev.flags &= ~NETDEV_IFF_RUNNING;
        kprint("e1000e: link down\n");
    }
    /* The speed-dependent fixups touch the PHY, which can busy-wait for a
     * second on the software flag: the worker does them, not the handler. */
    e2k.link_event = 1;
    sched_wakeup(&e2k.wd_chan);
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

    /* The interrupt probe runs before the netdev exists, and a recovery
     * reset rebuilds the rings under us. */
    if (!e2k.registered || e2k.resetting)
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
        if (e2k.link_status != STATUS_LU || e2k.resetting) {
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

/*
 * Multicast promiscuous stays on whatever IPv4 groups come and go: IPv6
 * neighbour discovery (ff02::1 and the solicited-node groups) needs it with
 * no IPv4 group joined at all.  The IP layer filters by membership.
 */
static void e2k_set_allmulti(netdev_t *dev, int on) {
    (void)dev; (void)on;
    e2k_write(R_RCTL, e2k_read(R_RCTL) | RCTL_MPE);
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
    *phys = (uint32_t)V2P(p);
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

/*
 * MAC configuration that every reset undoes.  Bits Intel's datasheets and
 * specification updates mark as required, per family.
 */
static void e2k_mac_init(void) {
    uint32_t ctrl, ext;

    /* Link: let the PHY autonegotiate; the MAC follows its result. */
    ctrl = e2k_read(R_CTRL);
    ctrl |= CTRL_SLU | CTRL_ASDE;
    ctrl &= ~(CTRL_LRST | CTRL_FRCSPD | CTRL_FRCDPLX);
    if (e2k.kind == K_82574)
        ctrl &= ~CTRL_BIT29;
    /* Lynx Point on: report packet-buffer memory errors rather than pass
     * a corrupted frame silently (with ECC enabled below). */
    if (e2k_is_pch())
        ctrl |= CTRL_MEHE;
    e2k_write(R_CTRL, ctrl);

    /* A driver owns the port now (manageability firmware backs off), and
     * the bit Intel documents as required on these families. */
    ext = e2k_read(R_CTRL_EXT) | CTRL_EXT_DRV_LOAD | CTRL_EXT_BIT22;
    if (e2k.kind == K_82574)
        ext &= ~CTRL_EXT_BIT23;
    /* Integrated MACs: strictly ordered DMA (no relaxed ordering)... */
    if (e2k_is_pch())
        ext |= CTRL_EXT_RO_DIS;
    e2k_write(R_CTRL_EXT, ext);

    if (e2k.kind == K_82574) {
        /* PCIe completion-timeout and request-ordering workarounds. */
        e2k_write(R_GCR, e2k_read(R_GCR) | GCR_BIT22);
        e2k_write(R_GCR2, e2k_read(R_GCR2) | GCR2_BIT0);
    } else {
        /* ...and snooped (no no-snoop bits). */
        e2k_write(R_GCR, e2k_read(R_GCR) & ~GCR_NO_SNOOP_ALL);
        e2k_write(R_KABGTXD, e2k_read(R_KABGTXD) | KABGTXD_BGSQLBIAS);
        e2k_write(R_PBECCSTS, e2k_read(R_PBECCSTS) | PBECCSTS_ECC_ENABLE);
    }

    /* The I219 legacy-interrupt clock fix does not survive a reset. */
    if (e2k.irq_kind != IRQ_NONE_K && e2k.irq_kind != IRQ_MSI)
        e2k_legacy_irq_fixup();
}

/*
 * Clear receive-address entries 1..N, so stale unicast filters a previous
 * driver left there do not pull other stations' frames in.  On the PCH the
 * shared entries may be write-locked by firmware (FWSM.WLOCK_MAC: 1 locks
 * them all, n > 1 leaves 1..n writable).
 */
static void e2k_clear_rar(void) {
    int n;

    if (e2k.kind == K_82574) {
        n = E1K2_RAR_82574;
    } else {
        int wlock = (int)((e2k_read(R_FWSM) & FWSM_WLOCK_MAC_MASK) >>
                          FWSM_WLOCK_MAC_SHIFT);
        if (wlock == 1)
            return;
        n = (wlock == 0 || wlock > E1K2_RAR_PCH) ? E1K2_RAR_PCH : wlock;
    }
    for (int i = 1; i <= n; i++) {
        e2k_write(R_RAH0 + (uint32_t)i * 8, 0);    /* Address Valid off first */
        e2k_write(R_RAL0 + (uint32_t)i * 8, 0);
    }
}

/*
 * Rings, filters and the transmit/receive units, from a freshly reset MAC.
 * The receive filter keeps RCTL.MPE: the multicast table would otherwise
 * have to carry ff02::1 and every solicited-node group, and IPv6 neighbour
 * discovery fails without them.  The IP layer filters by membership.
 */
static void e2k_mac_start(void) {
    uint32_t v, tctl;

    e2k_write_rar0(e2k.netdev.hwaddr);
    e2k_clear_rar();
    for (int i = 0; i < 128; i++)
        e2k_write(R_MTA + i * 4, 0);

    memset((void *)(uintptr_t)e2k.rx_ring, 0,
           E1K2_RX_DESCS * sizeof(struct e1k2_rx_desc));
    memset((void *)(uintptr_t)e2k.tx_ring, 0,
           E1K2_TX_DESCS * sizeof(struct e1k2_tx_desc));
    for (int i = 0; i < E1K2_RX_DESCS; i++)
        e2k.rx_ring[i].addr =
            (uint64_t)(e2k.rx_buf_phys + (uint32_t)i * E1K2_BUF_SIZE);
    e2k_barrier();

    /* Legacy RX descriptors; on the PCH, NFS filtering off -- it corrupts
     * descriptor write-back on NFSv2/UDP traffic. */
    v = e2k_read(R_RFCTL) & ~RFCTL_EXSTEN;
    if (e2k_is_pch())
        v |= RFCTL_NFSW_DIS | RFCTL_NFSR_DIS;
    e2k_write(R_RFCTL, v);

    e2k_write(R_RDBAL, e2k.rx_ring_phys);
    e2k_write(R_RDBAH, 0);
    e2k_write(R_RDLEN, E1K2_RX_DESCS * sizeof(struct e1k2_rx_desc));
    e2k_write(R_RDH, 0);
    e2k_write(R_RDT, E1K2_RX_DESCS - 1);
    e2k.rx_cur = 0;

    e2k_setup_tx_ring();
    e2k_write(R_TXDCTL, (e2k_read(R_TXDCTL) & ~(0x3Fu << 16)) |
                        TXDCTL_WTHRESH_1 | TXDCTL_GRAN | TXDCTL_BIT22);

    /* Copper defaults: IPGT 8, IPGR1 8, IPGR2 6. */
    e2k_write(R_TIPG, 8 | (8 << 10) | (6 << 20));

    /*
     * TCTL read-modify-write: MULR (set by reset) stays set.  The transmit
     * arbitration registers are left at reset values the datasheets mark
     * invalid unless set, per family.
     */
    tctl = e2k_read(R_TCTL) & ~((0xFFu << TCTL_CT_SHIFT) |
                                (0x3FFu << TCTL_COLD_SHIFT));
    tctl |= TCTL_PSP | TCTL_RTLC | TCTL_MULR |
            (0x0Fu << TCTL_CT_SHIFT) | (0x3Fu << TCTL_COLD_SHIFT);
    if (e2k.kind == K_82574) {
        v = e2k_read(R_TARC0);
        v &= ~TARC0_82574_CLEAR;
        v |= TARC0_82574_SET;
        e2k_write(R_TARC0, v);
    } else {
        e2k_write(R_TARC0, e2k_read(R_TARC0) | TARC0_PCH_SET);
        v = e2k_read(R_TARC1) | TARC1_PCH_SET;
        if (tctl & TCTL_MULR)
            v &= ~TARC1_MULR_INV;
        else
            v |= TARC1_MULR_INV;
        e2k_write(R_TARC1, v);
    }
    /*
     * Sunrise/Kaby Point I219 errata: IOSFPC works around transmit data
     * corruption, and the DMA engine is held to two outstanding read
     * requests instead of three ("buffer overrun while the I219 is
     * processing DMA transactions"), which otherwise corrupts payload or
     * hangs the transmit unit under sustained load.
     */
    if (e2k.flags & F_SPT_ERRATA) {
        e2k_write(R_IOSFPC, e2k_read(R_IOSFPC) | IOSFPC_RCTL_RDMTS_HEX);
        v = e2k_read(R_TARC0);
        v = (v & ~TARC0_CB_MULTIQ_MASK) | TARC0_CB_MULTIQ_2_REQ;
        e2k_write(R_TARC0, v);
    }
    e2k_write(R_TCTL, tctl | TCTL_EN);
    e2k_write(R_RCTL, RCTL_EN | RCTL_BAM | RCTL_MPE | RCTL_BSIZE_2048 |
                      RCTL_SECRC);
}

/*
 * With manageability pass-through active (AMT in shared-IP mode), firmware
 * leaves ARP interception on, so ARP requests matching its filter are
 * diverted away from the host ring -- and once DRV_LOAD is set, firmware
 * stops answering them.  The host then drops off IPv4 as peers' ARP caches
 * expire.  Take ARP back, and pass management traffic to the host too.
 */
static void e2k_mng_takeover(void) {
    uint32_t manc, fwsm;

    if (!e2k_is_pch())
        return;
    manc = e2k_read(R_MANC);
    fwsm = e2k_read(R_FWSM);
    if (!(manc & MANC_RCV_TCO_EN) ||
        (e2k_read(R_FACTPS) & FACTPS_MNGCG) ||
        ((fwsm & FWSM_MODE_MASK) >> FWSM_MODE_SHIFT) != FWSM_MODE_PT)
        return;
    e2k_write(R_MANC2H, e2k_read(R_MANC2H) | MANC2H_PORT_623 | MANC2H_PORT_664);
    e2k_write(R_MANC, (manc & ~MANC_ARP_EN) | MANC_EN_MNG2HOST);
    kprint("e1000e: manageability pass-through active; ARP taken over\n");
}

/* ----- link worker and recovery ----- */

static int e2k_phy_read(uint32_t preg, uint16_t *val) {
    int rc;

    if (e2k_swflag_acquire() != 0)
        return -2;
    rc = e2k_phy_read_locked(preg, val);
    e2k_swflag_release();
    return rc;
}

static int e2k_phy_write(uint32_t preg, uint16_t val) {
    int rc;

    if (e2k_swflag_acquire() != 0)
        return -2;
    rc = e2k_phy_write_locked(preg, val);
    e2k_swflag_release();
    return rc;
}

/* Kumeran (MAC-PHY interconnect) register access; caller holds the flag. */
static uint16_t e2k_kmrn_read_locked(uint32_t off) {
    e2k_write(R_KMRNCTRLSTA, ((off << KMRN_OFFSET_SHIFT) & KMRN_OFFSET_MASK) |
                             KMRN_REN);
    e2k_flush();
    timer_busywait_us(2);
    return (uint16_t)e2k_read(R_KMRNCTRLSTA);
}

static void e2k_kmrn_write_locked(uint32_t off, uint16_t val) {
    e2k_write(R_KMRNCTRLSTA, ((off << KMRN_OFFSET_SHIFT) & KMRN_OFFSET_MASK) |
                             val);
    e2k_flush();
    timer_busywait_us(2);
}

static unsigned e2k_link_speed(uint32_t st) {
    static const unsigned speeds[4] = { 10, 100, 1000, 1000 };
    return speeds[(st & STATUS_SPEED_MASK) >> STATUS_SPEED_SHIFT];
}

/*
 * The I218 on low-power platforms misses DMA completions at 1 Gb/s with K1
 * enabled: receive write-backs stop while the link stays up.  At 1 Gb/s,
 * K1 is dropped across setting FEXTNVM6.REQ_PLL_CLK; otherwise that bit is
 * cleared, and on early PHY revisions at 10 Mb/s or 100 half the in-band
 * link-status transmit timeout and K1 entry latency are set.
 */
static void e2k_k1_workaround_lpt_lp(int up, uint32_t st) {
    uint32_t fextnvm6 = e2k_read(R_FEXTNVM6);
    uint16_t reg;

    if (up && e2k_link_speed(st) == 1000) {
        if (e2k_swflag_acquire() != 0)
            return;
        reg = e2k_kmrn_read_locked(KMRN_K1_CONFIG);
        e2k_kmrn_write_locked(KMRN_K1_CONFIG, (uint16_t)(reg & ~KMRN_K1_ENABLE));
        timer_busywait_us(10);
        e2k_write(R_FEXTNVM6, fextnvm6 | FEXTNVM6_REQ_PLL_CLK);
        e2k_kmrn_write_locked(KMRN_K1_CONFIG, reg);
        e2k_swflag_release();
        return;
    }

    fextnvm6 &= ~FEXTNVM6_REQ_PLL_CLK;
    if (up && e2k.phy_rev <= 5 &&
        !(e2k_link_speed(st) == 100 && (st & STATUS_FD)) &&
        e2k_phy_read(I217_INBAND_CTRL, &reg) == 0) {
        reg &= ~I217_INBAND_TX_TIMEOUT_MASK;
        if (e2k_link_speed(st) == 100) {
            reg |= 5 << I217_INBAND_TX_TIMEOUT_SHIFT;       /* 5 x 10 us */
            fextnvm6 &= ~FEXTNVM6_K1_ENTRY_CONDITION;
        } else {
            reg |= 50 << I217_INBAND_TX_TIMEOUT_SHIFT;      /* 50 x 10 us */
            fextnvm6 |= FEXTNVM6_K1_ENTRY_CONDITION;
        }
        (void)e2k_phy_write(I217_INBAND_CTRL, reg);
    }
    e2k_write(R_FEXTNVM6, fextnvm6);
}

/* Decode an LTR value/scale pair into nanoseconds. */
static uint64_t e2k_ltr_ns(uint16_t enc) {
    return (uint64_t)(enc & LTRV_VALUE_MASK) <<
           (5 * ((enc & LTRV_SCALE_MASK) >> LTRV_SCALE_SHIFT));
}

/*
 * Latency Tolerance Reporting: how long the platform may take to service
 * receive DMA before the packet buffer overruns, so deep package C-states
 * do not drop frames.  Derived from the receive buffer size and the link
 * speed, clamped to the platform's limit in config space; requirements
 * cleared while the link is down.  OBFF gets the matching high-water mark.
 */
static void e2k_platform_pm(int up, uint32_t st) {
    uint32_t reg = LTRV_SEND;
    uint32_t hwm = 0;

    if (up) {
        unsigned speed = e2k_link_speed(st);
        uint32_t rxa = (e2k_read(R_PBA) & PBA_RXA_MASK) * 1024;   /* bytes */
        uint64_t lat_ns = 0;
        uint32_t value, scale = 0;
        uint16_t enc, max_snoop, max_nosnoop, max_enc;

        if (rxa > 2 * E1K2_MAX_FRAME_LTR)
            lat_ns = (uint64_t)(rxa - 2 * E1K2_MAX_FRAME_LTR) * 8 * 1000 / speed;
        value = (uint32_t)lat_ns;
        while (value > LTRV_VALUE_MASK) {
            scale++;
            value = (value + 31) / 32;
        }
        if (scale > LTRV_SCALE_MAX)
            return;
        enc = (uint16_t)((scale << LTRV_SCALE_SHIFT) | value);

        max_snoop = pci_read_config16(e2k.pdev->bus, e2k.pdev->slot,
                                      e2k.pdev->func, PCI_LTR_CAP_LPT);
        max_nosnoop = pci_read_config16(e2k.pdev->bus, e2k.pdev->slot,
                                        e2k.pdev->func, PCI_LTR_CAP_LPT + 2);
        max_enc = max_snoop > max_nosnoop ? max_snoop : max_nosnoop;
        if (e2k_ltr_ns(enc) > e2k_ltr_ns(max_enc))
            enc = max_enc;

        reg |= LTRV_SNOOP_REQ | LTRV_NOSNOOP_REQ | enc |
               ((uint32_t)enc << LTRV_NOSNOOP_SHIFT);

        /* Buffer left once the tolerated latency's worth has arrived. */
        uint64_t used = e2k_ltr_ns(enc) * speed / 8 / 1000;
        hwm = used < rxa ? (uint32_t)((rxa - used) / 1024) : 0;
        if (hwm > SVT_OFF_HWM_MASK)
            hwm = SVT_OFF_HWM_MASK;
    }
    e2k_write(R_LTRV, reg);
    e2k_write(R_SVT, (e2k_read(R_SVT) & ~SVT_OFF_HWM_MASK) | hwm);
    e2k_write(R_SVCR, e2k_read(R_SVCR) | SVCR_OFF_EN | SVCR_OFF_MASKINT);
}

/*
 * Speed-dependent settings the PCH parts need at every link change, from
 * the specification updates: transmit inter-packet gap and PHY receive
 * latency (10 half and 10/100 full collide or lose frames otherwise), PLL
 * clock-gate time (too short at 10/100 flaps the link), K1 clock request
 * at 1000, the I219 pointer gap, the 8 us beacon duration (I217 packet
 * loss), the Sunrise Point K1-off setting, the I218-LP K1 erratum and LTR.
 */
static void e2k_link_fixups(void) {
    uint32_t st = e2k_read(R_STATUS);
    int up = (st & STATUS_LU) != 0;
    unsigned speed = e2k_link_speed(st);
    uint16_t v;

    if (!e2k_is_pch())
        return;

    if (up) {
        uint32_t tipg = e2k_read(R_TIPG) & ~TIPG_IPGT_MASK;
        uint16_t emi;

        if (!(st & STATUS_FD) && speed == 10) {
            tipg |= 0xFF;
            emi = 0;
        } else if (e2k.kind == K_SPT && (st & STATUS_FD) && speed != 1000) {
            tipg |= 0x0C;
            emi = 1;
        } else {
            tipg |= 0x08;
            emi = 1;
        }
        e2k_write(R_TIPG, tipg);

        if (e2k_swflag_acquire() == 0) {
            if (e2k_phy_write_locked(PHY_REG(0, I82579_EMI_ADDR),
                                     I217_RX_CONFIG) == 0)
                (void)e2k_phy_write_locked(PHY_REG(0, I82579_EMI_DATA), emi);
            if (e2k_phy_read_locked(I217_PLL_CLOCK_GATE, &v) == 0) {
                v &= ~I217_PLL_CLOCK_GATE_MASK;
                v |= speed == 1000 ? 0x00FA : 0x03E8;
                (void)e2k_phy_write_locked(I217_PLL_CLOCK_GATE, v);
            }
            if (speed == 1000 && e2k_phy_read_locked(HV_PM_CTRL, &v) == 0)
                (void)e2k_phy_write_locked(HV_PM_CTRL,
                                           (uint16_t)(v | HV_PM_K1_CLK_REQ));
            if (e2k.kind == K_SPT) {
                if (speed == 1000) {
                    if (e2k_phy_read_locked(I219_PTR_GAP, &v) == 0 &&
                        ((v >> 2) & 0x3FF) < 0x18)
                        (void)e2k_phy_write_locked(I219_PTR_GAP,
                            (uint16_t)((v & ~(0x3FFu << 2)) | (0x18u << 2)));
                } else {
                    (void)e2k_phy_write_locked(I219_PTR_GAP, 0xC023);
                }
            }
            e2k_swflag_release();
        }
    }

    /* Reset can reload a wrong beacon duration from the NVM. */
    e2k_write(R_FEXTNVM4, (e2k_read(R_FEXTNVM4) & ~FEXTNVM4_BEACON_MASK) |
                          FEXTNVM4_BEACON_8US);

    if (e2k.flags & F_LPT_LP)
        e2k_k1_workaround_lpt_lp(up, st);

    if (e2k.flags & F_SPT_ERRATA) {
        uint32_t f6 = e2k_read(R_FEXTNVM6);
        if (e2k_read(R_PCIEANACFG) & FEXTNVM6_K1_OFF_ENABLE)
            f6 |= FEXTNVM6_K1_OFF_ENABLE;
        else
            f6 &= ~FEXTNVM6_K1_OFF_ENABLE;
        e2k_write(R_FEXTNVM6, f6);
    }

    e2k_platform_pm(up, st);
}

/*
 * Full reset and reprogramming, from the worker.  The PHY and its link are
 * left alone.  Transmit is refused and the handler keeps off the rings
 * while it runs; interrupts stay on, since the reset path can wait on
 * firmware for the software flag.
 */
static void e2k_recover(const char *why) {
    unsigned long flags = spinlock_acquire_irq(&e2k_tx_lock);
    e2k.resetting = 1;
    e2k_write(R_IMC, 0xFFFFFFFFu);
    spinlock_release_irq(&e2k_tx_lock, flags);

    kprintf("e1000e: %s; resetting the controller\n", why);
    e2k_hw_reset(e2k.pdev);
    e2k_mac_init();
    e2k_mac_start();

    flags = spinlock_acquire_irq(&e2k_tx_lock);
    e2k.resetting = 0;
    e2k_write(R_ITR, E1K2_ITR);
    e2k_write(R_IMS, E1K2_IMS);
    spinlock_release_irq(&e2k_tx_lock, flags);
}

/*
 * Link fixups on every link change, and recovery: a transmit unit that has
 * hung (TDH stuck behind TDT with link up), or frames left queued when the
 * link went down (the MAC does not complete them without link, so they
 * would sit in the ring for good).
 */
static void e2k_worker(void *arg) {
    uint32_t last_tdh = 0;
    unsigned stuck = 0;
    int was_up = (e2k.link_status == STATUS_LU);

    (void)arg;
    for (;;) {
        if (!e2k.link_event)
            sched_sleep_until(&e2k.wd_chan,
                              get_ticks() + (get_hz() * E1K2_WD_MS + 999) / 1000);

        int event = e2k.link_event;
        e2k.link_event = 0;
        int up = (e2k.link_status == STATUS_LU);
        uint32_t tdh = e2k_read(R_TDH), tdt = e2k_read(R_TDT);

        if (event || up != was_up) {
            e2k_link_fixups();
            if (was_up && !up && tdh != tdt)
                e2k_recover("link lost with frames queued");
            was_up = up;
            stuck = 0;
            continue;
        }

        if (up && tdh != tdt && tdh == last_tdh) {
            if (++stuck >= E1K2_TX_HANG_S * 1000 / E1K2_WD_MS) {
                e2k_recover("transmit unit hung");
                stuck = 0;
            }
        } else {
            stuck = 0;
        }
        last_tdh = tdh;
    }
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
    e2k.kind = kind & K_MASK;
    e2k.flags = kind & ~K_MASK;
    kind = e2k.kind;

    /* Memory space + bus mastering. */
    uint16_t cmd = pci_read_config16(pdev->bus, pdev->slot, pdev->func,
                                     PCI_CONFIG_COMMAND);
    pci_write_config16(pdev->bus, pdev->slot, pdev->func, PCI_CONFIG_COMMAND,
                       cmd | 0x0002 | 0x0004);

    /* 82574: unreliable PCIe completions are worst under ASPM, which
     * firmware may have enabled. */
    if (kind == K_82574)
        pci_disable_aspm(pdev);

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
    e2k_mac_init();
    e2k_mng_takeover();

    /* Station address: the reloaded RAR0, else what firmware had there. */
    if (e2k_read_rar0(mac) != 0) {
        if (!have_before) {
            kprint("e1000e: no valid MAC address in RAR0; not attaching\n");
            goto fail;
        }
        memcpy(mac, mac_before, 6);
    }
    memcpy(e2k.netdev.hwaddr, mac, 6);

    e2k_phy_bringup();
    e2k_mac_start();

    if (e2k_setup_irq(pdev) != 0)
        goto fail;

    strlcpy(e2k.netdev.name, "eth0", NETDEV_NAME_MAX);
    e2k.netdev.mtu = 1500;
    /* RUNNING follows the link (e2k_report_link()), not registration. */
    e2k.netdev.flags = NETDEV_IFF_UP | NETDEV_IFF_BROADCAST |
                       NETDEV_IFF_MULTICAST;
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

    /* Link fixups and recovery; link_event is already set, so the first
     * pass applies the fixups for the link found above. */
    thread_t *wt = NULL;
    if (kthread_create(e2k_worker, NULL, &wt, "e1000e") != 0)
        kprint("e1000e: no link worker; speed-dependent fixups and TX hang "
               "recovery are off\n");
    return 0;

fail:
    e2k_teardown(pdev);
    e2k.mmio = NULL;
    return -1;
}

/*
 * driver_data is the chip class plus per-ID flags.  I217/I218 (Lynx Point,
 * Wildcat Point) and I219 (Sunrise Point onwards).  Every PCH PHY from Lynx
 * Point on has ULP except the I217 and the I218-LM2/V2; the Sunrise/Kaby
 * Point I219s carry the transmit errata; the I218 on low-power platforms
 * has the 1 Gb/s K1 erratum.
 */
#define LPT_LP              (K_LPT | F_ULP | F_LPT_LP)
#define SPT_KBP             (K_SPT | F_ULP | F_SPT_ERRATA)
#define CNP                 (K_SPT | F_ULP | F_CNP)

static const device_id_t e2k_ids[] = {
    { E1K2_VENDOR, 0x10D3, 0, 0, K_82574 },   /* 82574L - qemu `e1000e` */
    { E1K2_VENDOR, 0x10F6, 0, 0, K_82574 },   /* 82574LA */
    { E1K2_VENDOR, 0x150C, 0, 0, K_82574 },   /* 82583V */
    { E1K2_VENDOR, 0x153A, 0, 0, K_LPT },     /* I217-LM */
    { E1K2_VENDOR, 0x153B, 0, 0, K_LPT },     /* I217-V */
    { E1K2_VENDOR, 0x155A, 0, 0, LPT_LP },    /* I218-LM */
    { E1K2_VENDOR, 0x1559, 0, 0, LPT_LP },    /* I218-V */
    { E1K2_VENDOR, 0x15A0, 0, 0, K_LPT },     /* I218-LM2 */
    { E1K2_VENDOR, 0x15A1, 0, 0, K_LPT },     /* I218-V2 */
    { E1K2_VENDOR, 0x15A2, 0, 0, LPT_LP },    /* I218-LM3 */
    { E1K2_VENDOR, 0x15A3, 0, 0, LPT_LP },    /* I218-V3 */
    { E1K2_VENDOR, 0x156F, 0, 0, SPT_KBP },   /* I219-LM */
    { E1K2_VENDOR, 0x1570, 0, 0, SPT_KBP },   /* I219-V */
    { E1K2_VENDOR, 0x15B7, 0, 0, SPT_KBP },   /* I219-LM2 */
    { E1K2_VENDOR, 0x15B8, 0, 0, SPT_KBP },   /* I219-V2 */
    { E1K2_VENDOR, 0x15B9, 0, 0, SPT_KBP },   /* I219-LM3 */
    { E1K2_VENDOR, 0x15D7, 0, 0, SPT_KBP },   /* I219-LM4 */
    { E1K2_VENDOR, 0x15D8, 0, 0, SPT_KBP },   /* I219-V4 */
    { E1K2_VENDOR, 0x15E3, 0, 0, SPT_KBP },   /* I219-LM5 */
    { E1K2_VENDOR, 0x15D6, 0, 0, SPT_KBP },   /* I219-V5 */
    { E1K2_VENDOR, 0x15BD, 0, 0, CNP },       /* I219-LM6 */
    { E1K2_VENDOR, 0x15BE, 0, 0, CNP },       /* I219-V6 */
    { E1K2_VENDOR, 0x15BB, 0, 0, CNP },       /* I219-LM7 */
    { E1K2_VENDOR, 0x15BC, 0, 0, CNP },       /* I219-V7 */
    { E1K2_VENDOR, 0x15DF, 0, 0, CNP },       /* I219-LM8 */
    { E1K2_VENDOR, 0x15E0, 0, 0, CNP },       /* I219-V8 */
    { E1K2_VENDOR, 0x15E1, 0, 0, CNP },       /* I219-LM9 */
    { E1K2_VENDOR, 0x15E2, 0, 0, CNP },       /* I219-V9 */
    { E1K2_VENDOR, 0x0D4E, 0, 0, CNP },       /* I219-LM10 */
    { E1K2_VENDOR, 0x0D4F, 0, 0, CNP },       /* I219-V10 */
    { E1K2_VENDOR, 0x0D4C, 0, 0, CNP },       /* I219-LM11 */
    { E1K2_VENDOR, 0x0D4D, 0, 0, CNP },       /* I219-V11 */
    { E1K2_VENDOR, 0x0D53, 0, 0, CNP },       /* I219-LM12 */
    { E1K2_VENDOR, 0x0D55, 0, 0, CNP },       /* I219-V12 */
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
