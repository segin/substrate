/*
 * e1000.c — Intel PRO/1000 (82540EM and friends) Gigabit Ethernet driver.
 *
 * This is the "legacy" e1000 register interface: MMIO BAR0, one RX and one
 * TX descriptor ring of 16-byte legacy descriptors, MAC read out of the
 * Receive Address registers.  It covers QEMU's `e1000` device family
 * (82540EM / 82544GC / 82545EM) and the physical 8254x/8257x parts that
 * speak the same register set.
 *
 * NOT covered: `e1000e` (82574L, 8086:10D3) and `igb` (82576).  Those use
 * an extended descriptor layout and a different init sequence; matching
 * their PCI IDs here would bind a driver that cannot drive them.  They need
 * their own drivers -- see the ID table at the bottom for what is claimed.
 *
 * Why this matters beyond QEMU: `e1000` is the default NIC on qemu's `pc`
 * machine, so a guest with no -device at all now gets a working interface
 * instead of nothing.
 *
 * QEMU: -netdev user,id=n0 -device e1000,netdev=n0
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

#define E1000_VENDOR        0x8086

/* Register offsets (BAR0, MMIO). */
#define E1000_CTRL          0x0000
#define E1000_STATUS        0x0008
#define E1000_ICR           0x00C0   /* interrupt cause read (read clears) */
#define E1000_IMS           0x00D0   /* interrupt mask set */
#define E1000_IMC           0x00D8   /* interrupt mask clear */
#define E1000_RCTL          0x0100
#define E1000_TCTL          0x0400
#define E1000_TIPG          0x0410
#define E1000_RDBAL         0x2800
#define E1000_RDBAH         0x2804
#define E1000_RDLEN         0x2808
#define E1000_RDH           0x2810
#define E1000_RDT           0x2818
#define E1000_TDBAL         0x3800
#define E1000_TDBAH         0x3804
#define E1000_TDLEN         0x3808
#define E1000_TDH           0x3810
#define E1000_TDT           0x3818
#define E1000_MTA           0x5200   /* multicast table array, 128 dwords */
#define E1000_RAL0          0x5400
#define E1000_RAH0          0x5404

/* CTRL bits. */
#define CTRL_SLU            (1u << 6)    /* set link up */
#define CTRL_ASDE           (1u << 5)    /* auto-speed detect enable */
#define CTRL_RST            (1u << 26)

/* RCTL bits. */
#define RCTL_EN             (1u << 1)
#define RCTL_UPE            (1u << 3)    /* unicast promiscuous */
#define RCTL_MPE            (1u << 4)    /* multicast promiscuous */
#define RCTL_LBM_NONE       (0u << 6)
#define RCTL_BAM            (1u << 15)   /* accept broadcast */
#define RCTL_BSIZE_2048     (0u << 16)
#define RCTL_SECRC          (1u << 26)   /* strip Ethernet CRC */

/* TCTL bits. */
#define TCTL_EN             (1u << 1)
#define TCTL_PSP            (1u << 3)    /* pad short packets */
#define TCTL_CT_SHIFT       4            /* collision threshold */
#define TCTL_COLD_SHIFT     12           /* collision distance */

/* Interrupt cause/mask bits. */
#define ICR_TXDW            (1u << 0)    /* transmit descriptor written back */
#define ICR_LSC             (1u << 2)    /* link status change */
#define ICR_RXDMT0          (1u << 4)    /* rx descriptor minimum threshold */
#define ICR_RXO             (1u << 6)    /* receiver overrun */
#define ICR_RXT0            (1u << 7)    /* receiver timer (packet waiting) */

/* Legacy receive descriptor status bits. */
#define RXD_STAT_DD         0x01
#define RXD_STAT_EOP        0x02

/* Legacy transmit descriptor command/status bits. */
#define TXD_CMD_EOP         0x01
#define TXD_CMD_IFCS        0x02   /* insert FCS */
#define TXD_CMD_RS          0x08   /* report status */
#define TXD_STAT_DD         0x01

#define E1000_RX_DESCS      32
#define E1000_TX_DESCS      16
#define E1000_BUF_SIZE      2048
#define E1000_MAX_FRAME     1518

/* Legacy descriptors are 16 bytes and the ring base must be 16-byte
 * aligned; both rings come out of page-aligned DMA memory. */
struct e1000_rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t csum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed));

struct e1000_tx_desc {
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
    uint8_t               irq;
    struct e1000_rx_desc *rx_ring;      /* direct-mapped virtual */
    uint32_t              rx_ring_phys;
    struct e1000_tx_desc *tx_ring;
    uint32_t              tx_ring_phys;
    uint8_t              *rx_buf;       /* E1000_RX_DESCS * E1000_BUF_SIZE */
    uint32_t              rx_buf_phys;
    uint8_t              *tx_buf;
    uint32_t              tx_buf_phys;
    uint32_t              rx_cur;
    uint32_t              tx_cur;
    netdev_t              netdev;
    int                   registered;
} e1k;

/*
 * The TX path is re-entered from the hard IRQ handler: e1000_irq ->
 * e1000_rx_drain -> netdev_rx -> inet_eth_input -> arp_input -> eth_send ->
 * netdev_xmit -> e1000_xmit, so an inbound ARP request transmits its reply
 * from inside the ISR.  This is the same hazard documented as RTL-02, and it
 * needs an IRQ-safe lock: a plain spinlock_acquire() deadlocks the moment the
 * ISR interrupts a lock holder.
 */
static spinlock_t e1k_tx_lock = SPINLOCK_INIT("e1000_tx");

static inline uint32_t e1k_read(uint32_t reg) {
    return *(volatile uint32_t *)(e1k.mmio + reg);
}

static inline void e1k_write(uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(e1k.mmio + reg) = val;
}

/* ----- RX path ----- */

static void e1000_rx_drain(void) {
    /*
     * Walk descriptors from our cursor while the NIC has marked them Done.
     * RDT is left one BEHIND the cursor: the tail must never equal the head,
     * or the hardware reads the ring as full and stops receiving.
     */
    for (;;) {
        struct e1000_rx_desc *d = &e1k.rx_ring[e1k.rx_cur];
        if (!(d->status & RXD_STAT_DD))
            break;

        uint16_t len = d->length;

        /*
         * `length` and `errors` come from the device.  A descriptor without
         * EOP is a jumbo/split frame we never configured for, and any error
         * bit means the bytes are not trustworthy -- drop rather than hand
         * either to the stack, but still recycle the descriptor so the ring
         * keeps moving.  (Not doing that is how a single bad frame wedges a
         * receiver permanently; cf. RTL-04.)
         */
        if ((d->status & RXD_STAT_EOP) && d->errors == 0 &&
            len >= 14 && len <= E1000_MAX_FRAME) {
            netdev_rx(&e1k.netdev, e1k.rx_buf + e1k.rx_cur * E1000_BUF_SIZE,
                      len);
        } else {
            e1k.netdev.rx_dropped++;
        }

        d->status = 0;                     /* hand the descriptor back */
        uint32_t prev = e1k.rx_cur;
        e1k.rx_cur = (e1k.rx_cur + 1) % E1000_RX_DESCS;
        e1k_write(E1000_RDT, prev);
    }
}

static int e1000_irq(unsigned int irq, void *dev_id, void *frame) {
    (void)irq; (void)dev_id; (void)frame;

    /* ICR is read-to-clear.  Reading it with no cause pending means this
     * was somebody else's interrupt on a shared line -- say so, or a
     * level-triggered line ends up unacknowledged forever. */
    uint32_t icr = e1k_read(E1000_ICR);
    if (icr == 0)
        return 0;

    if (icr & (ICR_RXT0 | ICR_RXDMT0 | ICR_RXO))
        e1000_rx_drain();

    /* TXDW needs no work: e1000_xmit reclaims by polling the DD bit, and
     * LSC is informational until we expose link state. */
    return 1;
}

/* ----- TX path ----- */

static int e1000_xmit(netdev_t *dev, const void *frame, size_t len) {
    (void)dev;
    if (!frame || len == 0) return -EINVAL;
    if (len > E1000_MAX_FRAME) return -EMSGSIZE;

    unsigned long flags = spinlock_acquire_irq(&e1k_tx_lock);

    uint32_t slot = e1k.tx_cur;
    struct e1000_tx_desc *d = &e1k.tx_ring[slot];

    /*
     * Wait for this descriptor's previous transmit to retire before reusing
     * its buffer.  Without it a sustained sender wraps the ring and memcpy()s
     * over a buffer the NIC is still reading, putting corrupt frames on the
     * wire -- the failure mode that made bulk upload (but not download) die
     * on rtl8139.  Bounded so a wedged NIC returns an error instead of
     * spinning forever with interrupts off.
     */
    if (d->cmd != 0 && !(d->status & TXD_STAT_DD)) {
        int spins = 0;
        while (!(d->status & TXD_STAT_DD)) {
            if (++spins > 1000000) {
                spinlock_release_irq(&e1k_tx_lock, flags);
                e1k.netdev.tx_dropped++;
                return -EIO;
            }
        }
    }

    memcpy(e1k.tx_buf + slot * E1000_BUF_SIZE, frame, len);

    d->addr   = (uint64_t)(e1k.tx_buf_phys + slot * E1000_BUF_SIZE);
    d->length = (uint16_t)len;
    d->cso    = 0;
    d->css    = 0;
    d->special = 0;
    d->status = 0;                              /* clear DD before handing over */
    d->cmd    = TXD_CMD_EOP | TXD_CMD_IFCS | TXD_CMD_RS;

    e1k.tx_cur = (slot + 1) % E1000_TX_DESCS;
    e1k_write(E1000_TDT, e1k.tx_cur);

    spinlock_release_irq(&e1k_tx_lock, flags);
    return 0;
}

static const struct netdev_ops e1000_ops = {
    .xmit = e1000_xmit,
};

/* ----- setup ----- */

static int e1000_setup(pci_device_t *pdev) {
    if (e1k.registered) {
        /* One instance for now: netdev naming and the single static state
         * block both assume it.  Say so rather than silently stomping. */
        kprint("e1000: additional controller ignored (single instance)\n");
        return -1;
    }

    /* Enable memory space + bus mastering.  Without BME the descriptor
     * rings are simply never read. */
    uint16_t cmd = pci_read_config16(pdev->bus, pdev->slot, pdev->func,
                                     PCI_CONFIG_COMMAND);
    pci_write_config16(pdev->bus, pdev->slot, pdev->func, PCI_CONFIG_COMMAND,
                       cmd | 0x0002 | 0x0004);

    e1k.mmio = pci_iomap(pdev, 0, 0x20000);
    if (!e1k.mmio) {
        kprint("e1000: could not map BAR0\n");
        return -1;
    }

    /* Mask every interrupt before touching the rings. */
    e1k_write(E1000_IMC, 0xFFFFFFFFu);
    (void)e1k_read(E1000_ICR);

    /* Bring the link up and let the PHY auto-negotiate. */
    e1k_write(E1000_CTRL, e1k_read(E1000_CTRL) | CTRL_SLU | CTRL_ASDE);

    /*
     * MAC address.  Read the Receive Address registers rather than walking
     * the EEPROM: RAL0/RAH0 are loaded from the EEPROM by hardware (and by
     * QEMU) before the driver ever runs, and the EEPROM access protocol
     * differs across the parts this driver claims.
     */
    uint32_t ral = e1k_read(E1000_RAL0);
    uint32_t rah = e1k_read(E1000_RAH0);
    e1k.netdev.hwaddr[0] = (uint8_t)(ral      );
    e1k.netdev.hwaddr[1] = (uint8_t)(ral >>  8);
    e1k.netdev.hwaddr[2] = (uint8_t)(ral >> 16);
    e1k.netdev.hwaddr[3] = (uint8_t)(ral >> 24);
    e1k.netdev.hwaddr[4] = (uint8_t)(rah      );
    e1k.netdev.hwaddr[5] = (uint8_t)(rah >>  8);

    /* Clear the multicast table; stale entries would accept traffic we
     * never asked for. */
    for (int i = 0; i < 128; i++)
        e1k_write(E1000_MTA + i * 4, 0);

    /* DMA memory.  pmm_alloc_contiguous returns a direct-mapped VIRTUAL
     * address; the NIC needs the physical one. */
    size_t rx_ring_bytes = E1000_RX_DESCS * sizeof(struct e1000_rx_desc);
    size_t tx_ring_bytes = E1000_TX_DESCS * sizeof(struct e1000_tx_desc);
    size_t rx_buf_bytes  = E1000_RX_DESCS * E1000_BUF_SIZE;
    size_t tx_buf_bytes  = E1000_TX_DESCS * E1000_BUF_SIZE;

    void *p;

    p = pmm_alloc_contiguous((rx_ring_bytes + 4095) / 4096);
    if (!p) { kprint("e1000: rx ring alloc failed\n"); return -1; }
    memset(p, 0, ((rx_ring_bytes + 4095) / 4096) * 4096);
    e1k.rx_ring = p;
    e1k.rx_ring_phys = (uint32_t)(uintptr_t)p - 0xC0000000u;

    p = pmm_alloc_contiguous((tx_ring_bytes + 4095) / 4096);
    if (!p) { kprint("e1000: tx ring alloc failed\n"); return -1; }
    memset(p, 0, ((tx_ring_bytes + 4095) / 4096) * 4096);
    e1k.tx_ring = p;
    e1k.tx_ring_phys = (uint32_t)(uintptr_t)p - 0xC0000000u;

    p = pmm_alloc_contiguous((rx_buf_bytes + 4095) / 4096);
    if (!p) { kprint("e1000: rx buffer alloc failed\n"); return -1; }
    memset(p, 0, ((rx_buf_bytes + 4095) / 4096) * 4096);
    e1k.rx_buf = p;
    e1k.rx_buf_phys = (uint32_t)(uintptr_t)p - 0xC0000000u;

    p = pmm_alloc_contiguous((tx_buf_bytes + 4095) / 4096);
    if (!p) { kprint("e1000: tx buffer alloc failed\n"); return -1; }
    memset(p, 0, ((tx_buf_bytes + 4095) / 4096) * 4096);
    e1k.tx_buf = p;
    e1k.tx_buf_phys = (uint32_t)(uintptr_t)p - 0xC0000000u;

    for (int i = 0; i < E1000_RX_DESCS; i++) {
        e1k.rx_ring[i].addr =
            (uint64_t)(e1k.rx_buf_phys + (uint32_t)i * E1000_BUF_SIZE);
        e1k.rx_ring[i].status = 0;
    }

    /* Receive ring.  RDH = 0, RDT = last descriptor: tail must trail head,
     * so the whole ring minus one entry is owned by the NIC. */
    e1k_write(E1000_RDBAL, e1k.rx_ring_phys);
    e1k_write(E1000_RDBAH, 0);
    e1k_write(E1000_RDLEN, (uint32_t)rx_ring_bytes);
    e1k_write(E1000_RDH, 0);
    e1k_write(E1000_RDT, E1000_RX_DESCS - 1);
    e1k.rx_cur = 0;

    /* Transmit ring: both pointers at 0, the ring is empty. */
    e1k_write(E1000_TDBAL, e1k.tx_ring_phys);
    e1k_write(E1000_TDBAH, 0);
    e1k_write(E1000_TDLEN, (uint32_t)tx_ring_bytes);
    e1k_write(E1000_TDH, 0);
    e1k_write(E1000_TDT, 0);
    e1k.tx_cur = 0;

    /* IEEE 802.3 default inter-packet gap (10/8/6). */
    e1k_write(E1000_TIPG, 10 | (8 << 10) | (6 << 20));

    e1k_write(E1000_TCTL, TCTL_EN | TCTL_PSP |
                          (0x0F << TCTL_CT_SHIFT) |
                          (0x40 << TCTL_COLD_SHIFT));

    /* Accept unicast-to-us and broadcast, 2 KiB buffers, strip the CRC so
     * netdev_rx sees exactly the frame.  No promiscuous bits: the stack
     * does not want other stations' traffic. */
    e1k_write(E1000_RCTL, RCTL_EN | RCTL_BAM | RCTL_LBM_NONE |
                          RCTL_BSIZE_2048 | RCTL_SECRC);

    /*
     * PCI INTx is shared -- see the sweep in ac97/hda/ahci.  Ask for sharing
     * and CHECK the answer: a NIC with no handler is a dead interface whose
     * only symptom appears much later as ENODEV from ifconfig.
     */
    e1k.irq = (uint8_t)pci_get_irq(pdev);
    if (e1k.irq) {
        int rc = request_irq(e1k.irq, e1000_irq, IRQF_SHARED, "e1000", &e1k);
        if (rc != 0) {
            kprintf("e1000: could not install IRQ %u handler (%d)%s\n",
                    (unsigned)e1k.irq, rc,
                    rc == -EBUSY ? " - line held exclusively by another driver"
                                 : "");
            return -1;
        }
    }

    /* Only now unmask, so no interrupt can arrive before the rings exist. */
    e1k_write(E1000_IMS, ICR_RXT0 | ICR_RXDMT0 | ICR_RXO | ICR_TXDW | ICR_LSC);

    strlcpy(e1k.netdev.name, "eth0", NETDEV_NAME_MAX);
    e1k.netdev.mtu = 1500;
    e1k.netdev.flags = NETDEV_IFF_UP | NETDEV_IFF_BROADCAST | NETDEV_IFF_RUNNING;
    e1k.netdev.ops = &e1000_ops;
    e1k.netdev.driver_data = &e1k;
    netdev_register(&e1k.netdev);
    e1k.registered = 1;

    kprintf("e1000: %02x:%02x:%02x:%02x:%02x:%02x irq %u\n",
            e1k.netdev.hwaddr[0], e1k.netdev.hwaddr[1], e1k.netdev.hwaddr[2],
            e1k.netdev.hwaddr[3], e1k.netdev.hwaddr[4], e1k.netdev.hwaddr[5],
            (unsigned)e1k.irq);
    return 0;
}

/*
 * Only parts that speak the LEGACY descriptor interface this driver
 * implements.  82574L (0x10D3, qemu's `e1000e`) and 82576 (`igb`) are
 * deliberately absent: they would bind and then not work.
 */
static const device_id_t e1000_ids[] = {
    /* Verified against qemu by reading `info pci` for each -device: */
    { E1000_VENDOR, 0x100E, 0, 0, 0 },   /* 82540EM - `e1000`, the pc default */
    { E1000_VENDOR, 0x100F, 0, 0, 0 },   /* 82545EM - `e1000-82545em` */
    { E1000_VENDOR, 0x100C, 0, 0, 0 },   /* 82544GC - `e1000-82544gc` */
    /* Same legacy register set, from the 8254x family table.  Untested here
     * for want of hardware, but harmless: a mismatch simply would not bind. */
    { E1000_VENDOR, 0x1004, 0, 0, 0 },   /* 82543GC copper */
    { E1000_VENDOR, 0x1008, 0, 0, 0 },   /* 82544EI copper */
    { E1000_VENDOR, 0x1009, 0, 0, 0 },   /* 82544EI fiber */
    { E1000_VENDOR, 0x1010, 0, 0, 0 },   /* 82546EB copper */
    { E1000_VENDOR, 0x1015, 0, 0, 0 },   /* 82540EM LOM */
    { 0, 0, 0, 0, 0 },
};

static int e1000_pci_attach(struct device *dev) {
    pci_device_t *pdev = pci_find_device_by_kdev(dev);
    if (!pdev) return -1;
    return e1000_setup(pdev);
}

static int e1000_pci_detach(struct device *dev) { (void)dev; return 0; }

static struct driver e1000_pci_driver = {
    .name = "e1000-pci",
    .id_table = e1000_ids,
    .attach = e1000_pci_attach,
    .detach = e1000_pci_detach,
};

void e1000_init(void) {
    static int registered;
    if (!registered) {
        (void)driver_register(&e1000_pci_driver, &pci_bus_type);
        registered = 1;
    }
}
