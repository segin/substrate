/*
 * rtl8139.c — Realtek RTL8139 PIO/DMA network driver.
 *
 * Classic NE2000-lineage 10/100 Ethernet controller, vendor 10EC
 * device 8139.  Single ring rx buffer (8 KiB + 16 + 1500 wrap pad),
 * four round-robin tx descriptors.
 *
 * Reads/writes go through I/O ports based on BAR0.  RX ring is a
 * contiguous DMA-coherent block of physical memory (allocated via
 * pmm_alloc_contiguous).  IRQ handler walks new packets out of the
 * ring and hands each to netdev_rx().
 *
 * QEMU: -netdev user,id=n0 -device rtl8139,netdev=n0
 */

#include <errno.h>
#include <string.h>

#include <arch/i386/intr.h>
#include <arch/i386/pmm.h>
#include <arch/x86-common/io.h>
#include <kern/console.h>
#include <kern/driver.h>
#include <kern/pci.h>
#include <sys/irq.h>
#include <sys/lock.h>
#include <sys/netdev.h>
#include <vm/vm_kmem.h>

/* PCI ID */
#define RTL8139_VENDOR  0x10EC
#define RTL8139_DEVICE  0x8139

/* Register offsets (BAR0) */
#define R_IDR0          0x00   /* MAC */
#define R_TSD0          0x10   /* TX status, 4 descriptors */
#define R_TSAD0         0x20   /* TX addr, 4 descriptors */
#define R_RBSTART       0x30   /* RX ring start (phys) */
#define R_CR            0x37   /* Command */
#define R_CAPR          0x38   /* Current Address of Packet Read */
#define R_IMR           0x3C   /* Interrupt Mask */
#define R_ISR           0x3E   /* Interrupt Status */
#define R_TCR           0x40   /* TX config */
#define R_RCR           0x44   /* RX config */
#define R_CONFIG1       0x52

/* CR bits */
#define CR_BUFE         0x01   /* RX buffer empty */
#define CR_TE           0x04   /* TX enable */
#define CR_RE           0x08   /* RX enable */
#define CR_RST          0x10   /* reset */

/* ISR bits */
#define ISR_ROK         0x0001
#define ISR_TOK         0x0004
#define ISR_RER         0x0002
#define ISR_TER         0x0008
#define ISR_RXOVW       0x0010   /* RX buffer overflow */
#define ISR_FOVW        0x0040   /* RX FIFO overflow */

/* RCR bits — accept broadcast + my MAC + multicast + WRAP */
#define RCR_AAP         0x01   /* accept all (promisc) */
#define RCR_APM         0x02   /* accept physical match */
#define RCR_AM          0x04   /* accept multicast */
#define RCR_AB          0x08   /* accept broadcast */
#define RCR_WRAP        0x80   /* wrap: extend ring 1500 bytes past end */

/* TX status bits */
#define TSD_OWN         0x2000
#define TSD_TOK         0x8000

#define RTL_RX_RING_SIZE  (8 * 1024 + 16 + 1500)
#define RTL_RX_PAGES      ((RTL_RX_RING_SIZE + 4095) / 4096)
#define RTL_TX_DESCS      4

static struct {
    uint16_t io_base;
    uint8_t  irq;
    uint8_t *rx_ring;        /* direct-mapped (virtual) */
    uint32_t rx_ring_phys;
    uint16_t rx_offset;      /* current read pointer in ring */
    uint8_t *tx_buf[RTL_TX_DESCS];
    uint32_t tx_buf_phys[RTL_TX_DESCS];
    int      tx_cur;
    uint8_t  tx_started[RTL_TX_DESCS];  /* slot has a TX in flight at least once */
    netdev_t netdev;
    int      registered;
} rtl;

/*
 * RTL-02: the TX path is re-entered from the hard IRQ handler with no
 * serialization at all.  rtl_irq -> rtl_rx_drain -> netdev_rx ->
 * inet_eth_input -> arp_input -> eth_send -> netdev_xmit -> rtl_xmit, so an
 * inbound ARP request transmits a reply from inside the ISR.  A
 * process-context rtl_xmit interrupted between its memcpy and the TSD write
 * has not yet advanced tx_cur, so the IRQ-side call takes the SAME slot,
 * overwrites the buffer and programs TSAD/TSD -- and on return the process
 * context writes TSD again for a descriptor the NIC now owns.
 *
 * Must be IRQ-safe: a plain spinlock_acquire() here deadlocks the instant
 * the ISR interrupts a lock holder, which is exactly the virtio-blk vblk_lock
 * bug fixed in 985b46796.
 */
static spinlock_t rtl_tx_lock = SPINLOCK_INIT("rtl_tx");

/* ----- RX path ----- */

/*
 * RTL-04: reset the receiver and resynchronize the ring.
 *
 * The old error path just `break`-ed out of the drain loop with a comment
 * claiming "let RX path reset on next IRQ" -- but no such reset existed
 * anywhere in the driver, and the break advanced neither rx_offset nor
 * CAPR.  The ring therefore stayed pointed at the bad packet forever and
 * the interface was permanently wedged after a single receive error.
 *
 * Recovery per the datasheet: stop the receiver, re-point RBSTART, clear
 * our read cursor and CAPR, then re-enable.  Anything still in the ring is
 * discarded, which is correct -- we no longer know where the packet
 * boundaries are.
 */
static void rtl_rx_reset(void) {
    uint8_t cr = inb(rtl.io_base + R_CR);
    outb(rtl.io_base + R_CR, (uint8_t)(cr & ~CR_RE));      /* RX off */
    rtl.rx_offset = 0;
    outl(rtl.io_base + R_RBSTART, rtl.rx_ring_phys);
    outw(rtl.io_base + R_CAPR, (uint16_t)(0 - 16));
    outl(rtl.io_base + R_RCR, RCR_APM | RCR_AB | RCR_AM | RCR_WRAP);
    outb(rtl.io_base + R_CR, (uint8_t)(cr | CR_RE));       /* RX on */
}

static void rtl_rx_drain(void) {
    while (!(inb(rtl.io_base + R_CR) & CR_BUFE)) {
        /* Each packet in the ring: 4-byte header (status:2, length:2)
         * followed by the raw Ethernet frame (length-4 bytes incl. CRC). */
        uint8_t *p = rtl.rx_ring + rtl.rx_offset;
        uint16_t status = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
        uint16_t length = (uint16_t)p[2] | ((uint16_t)p[3] << 8);
        if (!(status & 0x01)) {
            /* RTL-04: a receive error leaves the ring position unknown.
             * Reset the receiver rather than spinning on it forever. */
            rtl.netdev.rx_dropped++;
            rtl_rx_reset();
            return;
        }
        /*
         * RTL-04: `length` comes from the device.  It was used RAW to
         * advance the ring while only the netdev_rx call was gated on a
         * plausible range, so an early-receive header (0xFFF0, "packet
         * still arriving") or any absurd value walked rx_offset to a
         * position the device did not agree with -- desynchronizing the
         * ring permanently.  Validate before using it for anything.
         */
        if (length < 60 || length > 1518) {
            rtl.netdev.rx_dropped++;
            rtl_rx_reset();
            return;
        }
        /* Drop the trailing 4-byte CRC. */
        netdev_rx(&rtl.netdev, p + 4, length - 4);
        /* Advance past header + packet, then 4-byte align. */
        rtl.rx_offset = (uint16_t)((rtl.rx_offset + length + 4 + 3) & ~3);
        rtl.rx_offset = (uint16_t)(rtl.rx_offset % (8 * 1024));
        /* CAPR is 16 bytes BEHIND the actual read pointer — Realtek
         * quirk. */
        outw(rtl.io_base + R_CAPR, (uint16_t)(rtl.rx_offset - 16));
    }
}

/* ----- TX path ----- */

static int rtl_xmit(netdev_t *dev, const void *frame, size_t len) {
    (void)dev;
    if (len > 1792) return -EMSGSIZE;
    if (!frame || len == 0) return -EINVAL;
    /*
     * RTL-01: `len` is the caller's real frame length and must stay that way
     * until after the copy.  This used to clamp it UP to 60 here and then
     * memcpy(buf, frame, len), reading up to 40 bytes past the end of a
     * short frame -- and putting them on the wire.  (The memset below it was
     * dead: len was already >= 60 by then.)  Reachable from userland through
     * AF_PACKET, so the leaked bytes were whatever sat after the caller's
     * buffer, and a buffer ending on a page boundary faulted inside memcpy.
     * Copy exactly what we were given, zero the pad, and only then round the
     * length up for the hardware.
     */
    unsigned long txf = spinlock_acquire_irq(&rtl_tx_lock);   /* RTL-02 */
    int slot = rtl.tx_cur;

    /* Wait for this descriptor's previous transmit to finish before
     * reusing its DMA buffer.  The chip has only 4 TX descriptors; a
     * sustained sender (a bulk TCP upload) wraps the ring in
     * microseconds.  Without this wait we memcpy() a new frame over a
     * buffer the NIC is still DMA'ing out, corrupting frames on the
     * wire — the peer never ACKs them, substrate's RTO fires
     * TCP_MAX_RETX times, and the connection dies with ETIMEDOUT (and
     * the ring is left wedged, so the next connect() can't even send
     * its SYN).  A receive-mostly workload sends only sparse ACKs and
     * never wraps the ring, which is why bulk download worked but bulk
     * upload didn't.  OWN (bit 13) is set by the NIC when it has
     * finished moving the descriptor's data into the TX FIFO.  Only
     * wait on a slot we've actually used (its initial TSD state is
     * don't-care). */
    if (rtl.tx_started[slot]) {
        /* RTL-06: this poll can run inside rtl_irq with IF=0, where seconds
         * of spinning freeze the machine.  Bound it much more tightly when
         * we cannot afford to wait, and just drop the frame -- the upper
         * layer retransmits. */
        uint32_t limit = intr_enabled() ? 2000000u : 10000u;
        uint32_t spins = 0;
        while (!(inl(rtl.io_base + R_TSD0 + slot * 4) & TSD_OWN)) {
            if (++spins > limit) {
                spinlock_release_irq(&rtl_tx_lock, txf);
                return -EBUSY;                        /* NIC wedged / busy */
            }
            __asm__ volatile("pause");
        }
    }

    uint8_t *buf = rtl.tx_buf[slot];
    memcpy(buf, frame, len);                 /* exactly what we were given */
    if (len < 60) {
        memset(buf + len, 0, 60 - len);      /* pad with zeroes, not memory */
        len = 60;                            /* minimum Ethernet frame */
    }
    outl(rtl.io_base + R_TSAD0 + slot * 4, rtl.tx_buf_phys[slot]);
    outl(rtl.io_base + R_TSD0  + slot * 4, (uint32_t)len);
    rtl.tx_started[slot] = 1;
    rtl.tx_cur = (slot + 1) % RTL_TX_DESCS;
    spinlock_release_irq(&rtl_tx_lock, txf);
    return 0;
}

/* ----- IRQ ----- */

static int rtl_irq(unsigned int irq, void *dev_id, void *frame) {
    (void)irq; (void)dev_id; (void)frame;
    uint16_t isr = inw(rtl.io_base + R_ISR);
    if (!isr) return 0;
    /* Ack first to avoid losing edges. */
    outw(rtl.io_base + R_ISR, isr);
    if (isr & ISR_ROK) rtl_rx_drain();
    /* RTL-05: an overflow means the ring position is no longer trustworthy;
     * drain whatever is intact, then resynchronize. */
    if (isr & (ISR_RXOVW | ISR_FOVW)) {
        rtl.netdev.rx_dropped++;
        rtl_rx_reset();
    }
    if (isr & ISR_RER) rtl.netdev.rx_dropped++;
    return 1;
}

static const struct netdev_ops rtl_ops = { .xmit = rtl_xmit };

/* ----- attach ----- */

int rtl8139_setup(pci_device_t *pdev) {
    if (rtl.registered) return 0;
    if (!pdev) return -1;

    /* BAR0 is I/O space on the RTL8139. */
    uint32_t bar0 = pci_read(pdev->bus, pdev->slot, pdev->func, 0x10);
    if (!(bar0 & 1)) {
        kprint("rtl8139: BAR0 not I/O\n");
        return -1;
    }
    rtl.io_base = (uint16_t)(bar0 & 0xFFFC);
    int irq = pci_get_irq(pdev);
    if (irq != PCI_IRQ_NONE) rtl.irq = (uint8_t)irq;

    /* Enable bus mastering. */
    uint32_t cmd = pci_read(pdev->bus, pdev->slot, pdev->func, 0x04);
    pci_write(pdev->bus, pdev->slot, pdev->func, 0x04, cmd | 0x07);

    /* Power on (CONFIG1=0x00 keeps it awake on most chips). */
    outb(rtl.io_base + R_CONFIG1, 0x00);

    /* Soft reset. */
    outb(rtl.io_base + R_CR, CR_RST);
    for (int i = 0; i < 1000 && (inb(rtl.io_base + R_CR) & CR_RST); i++) {}

    /* Read MAC. */
    for (int i = 0; i < 6; i++) {
        rtl.netdev.hwaddr[i] = inb(rtl.io_base + R_IDR0 + i);
    }

    /* Allocate RX ring (3 contiguous pages = 12 KiB, covers
     * 8 KiB + 16 + 1500 + slack). */
    void *rxvirt = pmm_alloc_contiguous(RTL_RX_PAGES);
    if (!rxvirt) { kprint("rtl8139: rx alloc fail\n"); return -1; }
    memset(rxvirt, 0, RTL_RX_PAGES * 4096);
    rtl.rx_ring = (uint8_t *)rxvirt;
    rtl.rx_ring_phys = (uint32_t)rxvirt - 0xC0000000;
    outl(rtl.io_base + R_RBSTART, rtl.rx_ring_phys);

    /* Allocate TX buffers — one page each, plenty for max frame. */
    for (int i = 0; i < RTL_TX_DESCS; i++) {
        rtl.tx_buf[i] = (uint8_t *)pmm_alloc_block();
        memset(rtl.tx_buf[i], 0, 4096);
        rtl.tx_buf_phys[i] = (uint32_t)rtl.tx_buf[i] - 0xC0000000;
    }

    /* Configure RX: APM | AB | AM | WRAP, 8 KiB ring (RBLEN=00). */
    outl(rtl.io_base + R_RCR, RCR_APM | RCR_AB | RCR_AM | RCR_WRAP);
    /* Configure TX: default. */
    outl(rtl.io_base + R_TCR, (3 << 8));   /* MaxDMA = 1 KiB */

    /* Enable interrupts for ROK + TOK + errors. */
    /* RTL-05: RxOverflow and RxFIFOOver were masked off and unhandled, so
     * an overflow silently wedged the ring with no way to notice.  Take
     * them and recover in the ISR. */
    outw(rtl.io_base + R_IMR,
         ISR_ROK | ISR_TOK | ISR_RER | ISR_TER | ISR_RXOVW | ISR_FOVW);

    /* Start RX + TX. */
    outb(rtl.io_base + R_CR, CR_RE | CR_TE);

    /* RTL-03: PCI INTx is routinely shared (this NIC lands on the same line
     * as virtio-blk / AHCI under QEMU).  Without IRQF_SHARED one of the two
     * registrations is refused with -EBUSY: either the NIC gets no handler
     * and the interface is dead, or the sibling loses its own.  And since
     * INTx is level-triggered, an unserviced line re-delivers forever --
     * the storm this project already hit with AHCI.  Ask for sharing, and
     * check the answer instead of discarding it. */
    if (rtl.irq) {
        int rc = request_irq(rtl.irq, rtl_irq, IRQF_SHARED, "rtl8139", &rtl);
        if (rc != 0) {
            kprint("rtl8139: could not install IRQ handler\n");
            return -1;
        }
    }

    /* Register. */
    strlcpy(rtl.netdev.name, "eth0", NETDEV_NAME_MAX);
    rtl.netdev.mtu = 1500;
    rtl.netdev.flags = NETDEV_IFF_UP | NETDEV_IFF_BROADCAST | NETDEV_IFF_RUNNING;
    rtl.netdev.ops = &rtl_ops;
    rtl.netdev.driver_data = &rtl;
    netdev_register(&rtl.netdev);
    rtl.registered = 1;
    return 0;
}

static const device_id_t rtl8139_ids[] = {
    { RTL8139_VENDOR, RTL8139_DEVICE, 0, 0, 0 },
    { 0, 0, 0, 0, 0 },
};

static int rtl8139_pci_attach(struct device *dev) {
    pci_device_t *pdev = pci_find_device_by_kdev(dev);
    if (!pdev) return -1;
    return rtl8139_setup(pdev);
}

static int rtl8139_pci_detach(struct device *dev) { (void)dev; return 0; }

static struct driver rtl8139_pci_driver = {
    .name = "rtl8139-pci",
    .id_table = rtl8139_ids,
    .attach = rtl8139_pci_attach,
    .detach = rtl8139_pci_detach,
};

void rtl8139_init(void) {
    static int registered;
    if (!registered) {
        (void)driver_register(&rtl8139_pci_driver, &pci_bus_type);
        registered = 1;
    }
}
