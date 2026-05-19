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

#include <sys/netdev.h>
#include <sys/irq.h>
#include <arch/x86-common/io.h>
#include <arch/i386/pmm.h>
#include <kern/console.h>
#include <kern/pci.h>
#include <kern/driver.h>
#include <vm/vm_kmem.h>
#include <string.h>
#include <errno.h>

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
    netdev_t netdev;
    int      registered;
} rtl;

/* ----- RX path ----- */

static void rtl_rx_drain(void) {
    while (!(inb(rtl.io_base + R_CR) & CR_BUFE)) {
        /* Each packet in the ring: 4-byte header (status:2, length:2)
         * followed by the raw Ethernet frame (length-4 bytes incl. CRC). */
        uint8_t *p = rtl.rx_ring + rtl.rx_offset;
        uint16_t status = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
        uint16_t length = (uint16_t)p[2] | ((uint16_t)p[3] << 8);
        if (!(status & 0x01)) {
            /* Receive error — bail and let RX path reset on next IRQ. */
            break;
        }
        if (length >= 60 && length <= 1518) {
            /* Drop the trailing 4-byte CRC. */
            netdev_rx(&rtl.netdev, p + 4, length - 4);
        }
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
    if (len < 60) len = 60;   /* pad short frames; ignore upper layer's len */
    int slot = rtl.tx_cur;
    uint8_t *buf = rtl.tx_buf[slot];
    memcpy(buf, frame, len);
    if (len < 60) memset(buf + len, 0, 60 - len);
    outl(rtl.io_base + R_TSAD0 + slot * 4, rtl.tx_buf_phys[slot]);
    outl(rtl.io_base + R_TSD0  + slot * 4, (uint32_t)len);
    rtl.tx_cur = (slot + 1) % RTL_TX_DESCS;
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
    outw(rtl.io_base + R_IMR, ISR_ROK | ISR_TOK | ISR_RER | ISR_TER);

    /* Start RX + TX. */
    outb(rtl.io_base + R_CR, CR_RE | CR_TE);

    if (rtl.irq) request_irq(rtl.irq, rtl_irq, 0, "rtl8139", &rtl);

    /* Register. */
    strncpy(rtl.netdev.name, "eth0", NETDEV_NAME_MAX - 1);
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
