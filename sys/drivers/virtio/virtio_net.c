/*
 * virtio_net.c — VirtIO 1.0 legacy-IO network driver.
 *
 * Two virtqueues: 0 = receive, 1 = transmit.
 * RX queue is pre-populated with empty 1526-byte buffers; the device
 * DMAs incoming frames into them.  IRQ handler walks the used ring,
 * passes each frame to netdev_rx(), and recycles the buffer.
 * TX queue allocates a desc per call to xmit, kicks the device,
 * waits for completion (or just trusts the device — TX completion
 * is fire-and-forget here).
 *
 * Limits: 16 buffers each direction (good enough for DHCP + simple
 * tests; bump VNET_QSZ for higher throughput).  Single-NIC.
 *
 * QEMU: -netdev user,id=n0 -device virtio-net-pci,netdev=n0
 */

#include <errno.h>
#include <string.h>

#include <arch/i386/intr.h>
#include <arch/i386/pmm.h>
#include <arch/x86-common/io.h>
#include <drivers/virtio/virtio.h>
#include <kern/console.h>
#include <kern/pci.h>
#include <sys/irq.h>
#include <sys/lock.h>
#include <sys/netdev.h>

#define VIRTIO_NET_HDR_LEN 10
#define VNET_FRAME_MAX     1526        /* 14 + 1500 + 12 */
#define VNET_BUFS          16          /* per-direction buffer pool */
/*
 * Largest device-reported queue size we will back.  This bounds ONLY the
 * contiguous ring allocation -- the data-buffer pool is VNET_BUFS and is
 * independent of it, since n_bufs is min(qsz, VNET_BUFS) and a ring may
 * legitimately have more slots than we choose to populate.  4096 entries
 * needs ~27 contiguous pages, which is obtainable; the 32768 a legacy
 * device may legally report would need ~208 and is refused rather than
 * mismatched.  (This was 256 "matching the QEMU default", which is what
 * made rx_queue_size=1024 -- a perfectly ordinary setting -- trip the
 * VNET-01 mismatch.)
 */
#define VNET_QSZ_MAX       4096

struct virtio_net_hdr {
    uint8_t  flags;
    uint8_t  gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
} __attribute__((packed));

typedef struct vnet_queue {
    uint16_t              q_size;          /* real queue size as set by device */
    uint16_t              n_bufs;          /* number of slots we actually use */
    struct vring_desc    *desc;
    struct vring_avail   *avail;
    struct vring_used    *used;
    uint16_t              last_used_idx;
    void                 *bufs[VNET_BUFS]; /* per-desc data buffer (frame+hdr) */
} vnet_queue_t;

static struct {
    uint16_t       io_base;
    uint8_t        irq;
    vnet_queue_t   rxq;
    vnet_queue_t   txq;
    netdev_t       netdev;
    int            registered;
} vn;

/* VNET-02: see vnet_xmit.  IRQ-safe: the TX path is re-entered from the
 * NIC's own interrupt handler. */
static spinlock_t vnet_tx_lock = SPINLOCK_INIT("vnet_tx");

/* ----- virtqueue setup helpers ----- */

static int vnet_queue_init(vnet_queue_t *q, uint16_t qsel) {
    outw(vn.io_base + VIRTIO_REG_QUEUE_SELECT, qsel);
    uint16_t qsz = inw(vn.io_base + VIRTIO_REG_QUEUE_SIZE);

    /*
     * VNET-01: QUEUE_NUM is READ-ONLY in legacy virtio-pci -- the device
     * fixes the size and we do not get a vote.  This used to clamp qsz down
     * to VNET_QSZ_MAX and then size the ring allocation from the clamped
     * value, while the device went on computing the used-ring offset from
     * its own real size.  With rx_queue_size=1024 the device placed the used
     * ring at align(16*1024 + 6 + 2048, 4096) = 20480 while we had allocated
     * 3 pages for 256 entries, so it wrote roughly 8 KiB past our allocation
     * into memory the buddy allocator had handed to somebody else.  The
     * comment immediately below already stated the rule -- "we must match
     * exactly or it scribbles into space we don't own" -- and the clamp
     * broke it.
     *
     * Allocate for the size the device actually reports.  If that is larger
     * than we are prepared to back, fail the attach rather than silently
     * mismatching.  qsz == 0 means the queue is absent, and we must not
     * program QUEUE_ADDR for it at all.
     */
    if (qsz == 0) return -ENODEV;
    if (qsz > VNET_QSZ_MAX) {
        kprintf("virtio-net: queue %u size %u exceeds supported %u\n",
                qsel, qsz, (unsigned)VNET_QSZ_MAX);
        return -ENOTSUP;
    }
    q->q_size = qsz;
    q->n_bufs = (qsz < VNET_BUFS) ? qsz : VNET_BUFS;

    /* Per VirtIO 1.0 legacy: ring layout (sized by REAL queue size,
     * not by what we choose to actually use) is
     *   desc[qsz] at offset 0                 (16*qsz bytes)
     *   avail     at offset 16*qsz            (6 + 2*qsz bytes)
     *   used      at next 4096-aligned offset (6 + 8*qsz bytes)
     * Device computes the used-ring offset itself from qsz; we must
     * match exactly or it scribbles into space we don't own. */
    uint32_t avail_end = 16u * qsz + 6 + 2u * qsz;
    uint32_t used_off  = (avail_end + 4095) & ~4095;
    uint32_t used_end  = used_off + 6 + 8u * qsz;
    size_t   pages     = (used_end + 4095) / 4096;
    if (pages < 1) pages = 1;

    void *page = pmm_alloc_contiguous(pages);
    if (!page) {                    /* VNET-11: never memset(NULL) */
        kprint("virtio-net: ring allocation failed\n");
        return -ENOMEM;
    }
    memset(page, 0, pages * 4096);
    uint32_t page_phys = (uint32_t)page - 0xC0000000;

    q->desc  = (struct vring_desc *)page;
    q->avail = (struct vring_avail *)((uint8_t *)page + 16 * qsz);
    q->used  = (struct vring_used *)((uint8_t *)page + used_off);
    q->last_used_idx = 0;

    /* Allocate one buffer per slot we actually use. */
    for (int i = 0; i < q->n_bufs; i++) {
        void *buf = pmm_alloc_block();
        if (!buf) {                 /* VNET-11 */
            kprint("virtio-net: buffer allocation failed\n");
            return -ENOMEM;
        }
        memset(buf, 0, 4096);
        q->bufs[i] = buf;
    }

    outl(vn.io_base + VIRTIO_REG_QUEUE_ADDR, page_phys >> 12);
    return 0;
}

/* ----- RX path ----- */

static void vnet_rx_post(int i) {
    vnet_queue_t *q = &vn.rxq;
    uint32_t buf_phys = (uint32_t)q->bufs[i] - 0xC0000000;
    q->desc[i].addr  = buf_phys;
    q->desc[i].len   = VIRTIO_NET_HDR_LEN + VNET_FRAME_MAX;
    q->desc[i].flags = VRING_DESC_F_WRITE;   /* device writes into us */
    q->desc[i].next  = 0;
}

static void vnet_rx_publish_all(void) {
    vnet_queue_t *q = &vn.rxq;
    for (int i = 0; i < q->n_bufs; i++) {
        vnet_rx_post(i);
        q->avail->ring[i] = (uint16_t)i;
    }
    /* VNET-05: the descriptor and ring writes above must be visible to the
     * device before the index that publishes them. */
    __asm__ volatile("" ::: "memory");
    q->avail->idx = q->n_bufs;
    /* Kick the device — we have n_bufs rx buffers ready. */
    outw(vn.io_base + VIRTIO_REG_QUEUE_NOTIFY, 0);
}

static void vnet_rx_drain(void) {
    vnet_queue_t *q = &vn.rxq;
    while (q->last_used_idx != q->used->idx) {
        uint16_t slot = q->last_used_idx % q->q_size;
        struct vring_used_elem *u = &q->used->ring[slot];
        uint32_t desc_id = u->id;
        uint32_t total   = u->len;
        /* desc_id comes straight from the device-owned used ring; a
         * malicious or buggy device can put any 16-bit value here.  We only
         * ever posted ids 0..n_bufs-1, so anything else would index
         * q->bufs[]/q->desc[] out of bounds (kernel OOB read/write).  Drop
         * the malformed completion instead. */
        if (desc_id >= q->n_bufs) {
            q->last_used_idx++;
            continue;
        }
        if (total > VIRTIO_NET_HDR_LEN) {
            uint8_t *buf = (uint8_t *)q->bufs[desc_id];
            /* Skip the virtio_net_hdr prefix.  Substrate's AF_PACKET
             * speaks raw Ethernet — no virtio header exposed. */
            netdev_rx(&vn.netdev, buf + VIRTIO_NET_HDR_LEN,
                      total - VIRTIO_NET_HDR_LEN);
        }
        /* Recycle the buffer back to the device. */
        vnet_rx_post((int)desc_id);
        q->avail->ring[q->avail->idx % q->q_size] = (uint16_t)desc_id;
        __asm__ volatile("" ::: "memory");   /* VNET-05: publish after writes */
        q->avail->idx++;
        q->last_used_idx++;
    }
    outw(vn.io_base + VIRTIO_REG_QUEUE_NOTIFY, 0);
}

/* ----- TX path ----- */

static int vnet_xmit(netdev_t *dev, const void *frame, size_t len) {
    (void)dev;
    if (len > VNET_FRAME_MAX) return -EMSGSIZE;
    if (!frame || len == 0) return -EINVAL;
    vnet_queue_t *q = &vn.txq;

    /*
     * VNET-02: vnet_xmit is re-entered from the hard IRQ handler with no
     * serialization -- vnet_irq -> vnet_rx_drain -> netdev_rx ->
     * inet_eth_input -> arp_input -> eth_send -> netdev_xmit -> vnet_xmit.
     * desc_id is derived from q->avail->idx before that index is
     * incremented, so an interrupting call computed the SAME desc_id and
     * published the same descriptor twice.  IRQ-safe by necessity: a plain
     * spinlock would deadlock when the ISR interrupts the holder.
     */
    unsigned long txf = spinlock_acquire_irq(&vnet_tx_lock);

    /*
     * VNET-03: reap the TX used ring before reusing a buffer.  Nothing ever
     * read txq.used->idx -- last_used_idx was written once at init and never
     * again -- so after n_bufs transmits we memcpy'd a new frame into a
     * buffer the device might still be reading.  This is the same bug class
     * already diagnosed and fixed on the rtl8139 side (see the comment on
     * its TSD_OWN wait); it is masked under QEMU only because QEMU drains TX
     * synchronously inside the notify write, so it shows up as corrupted
     * frames on a sustained upload against real hardware.
     */
    uint32_t in_flight = (uint32_t)(uint16_t)(q->avail->idx - q->last_used_idx);
    if (in_flight >= (uint32_t)q->n_bufs) {
        uint32_t spins = 0;
        uint32_t limit = intr_enabled() ? 2000000u : 10000u;
        while ((uint16_t)(q->avail->idx - q->last_used_idx) >=
               (uint16_t)q->n_bufs) {
            if (q->last_used_idx != q->used->idx) {
                q->last_used_idx++;          /* one completion reaped */
                continue;
            }
            if (++spins > limit) {
                spinlock_release_irq(&vnet_tx_lock, txf);
                return -EBUSY;               /* device not draining */
            }
            __asm__ volatile("pause");
        }
    }

    /* Round-robin within our small buffer pool (n_bufs). */
    int desc_id = q->avail->idx % q->n_bufs;
    uint8_t *buf = (uint8_t *)q->bufs[desc_id];
    memset(buf, 0, VIRTIO_NET_HDR_LEN);   /* zero header */
    memcpy(buf + VIRTIO_NET_HDR_LEN, frame, len);

    uint32_t buf_phys = (uint32_t)buf - 0xC0000000;
    q->desc[desc_id].addr  = buf_phys;
    q->desc[desc_id].len   = VIRTIO_NET_HDR_LEN + len;
    q->desc[desc_id].flags = 0;   /* read by device */
    q->desc[desc_id].next  = 0;

    /* Avail ring is sized by the REAL queue size; modulus must
     * match what the device uses. */
    q->avail->ring[q->avail->idx % q->q_size] = (uint16_t)desc_id;
    __asm__ volatile("" ::: "memory");
    q->avail->idx++;
    outw(vn.io_base + VIRTIO_REG_QUEUE_NOTIFY, 1);
    spinlock_release_irq(&vnet_tx_lock, txf);
    return 0;
}

/* ----- IRQ handler ----- */

static int vnet_irq(unsigned int irq, void *dev_id, void *frame) {
    (void)irq; (void)dev_id; (void)frame;
    uint8_t isr = inb(vn.io_base + VIRTIO_REG_ISR_STATUS);
    if (!(isr & 0x01)) return 0;   /* not for us */
    vnet_rx_drain();
    return 1;
}

/* ----- ops ----- */

static const struct netdev_ops vnet_ops = { .xmit = vnet_xmit };

/* ----- attach (called from sys/drivers/virtio/virtio.c dispatch) ----- */

int virtio_net_setup(uint8_t bus, uint8_t slot, uint8_t func) {
    if (vn.registered) return 0;
    vn.io_base = virtio_get_io_base(bus, slot, func);
    if (!vn.io_base) {
        kprint("virtio-net: no I/O base; bailing\n");
        return -1;
    }
    /* Walk the global PCI device list to find our bus/slot/func and
     * extract the assigned IRQ line. */
    for (pci_device_t *p = pci_first_device(); p; p = pci_next_device(p)) {
        if (p->bus == bus && p->slot == slot && p->func == func) {
            int irq = pci_get_irq(p);
            if (irq != PCI_IRQ_NONE) vn.irq = (uint8_t)irq;
            break;
        }
    }

    /* Enable PCI I/O decoding + bus-mastering.  Bus-master (bit 2) is
     * mandatory: virtio is all DMA — the device reads descriptors and
     * the avail/used rings out of guest memory and writes RX frames
     * into our buffers.  Without it the queues never advance, so DHCP
     * (and everything else) hangs.  rtl8139 sets the same bits. */
    uint32_t cmd = pci_read(bus, slot, func, PCI_CONFIG_COMMAND);
    pci_write(bus, slot, func, PCI_CONFIG_COMMAND,
              cmd | PCI_COMMAND_IO | PCI_COMMAND_MASTER);

    /* 1. Reset + acknowledge + driver bits. */
    outb(vn.io_base + VIRTIO_REG_DEVICE_STATUS, 0);
    outb(vn.io_base + VIRTIO_REG_DEVICE_STATUS, 1);  /* ACKNOWLEDGE */
    outb(vn.io_base + VIRTIO_REG_DEVICE_STATUS, 3);  /* ACK | DRIVER */

    /* 2. Negotiate features — accept the MAC feature only. */
    uint32_t host_feat = inl(vn.io_base + VIRTIO_REG_HOST_FEATURES);
    uint32_t guest_feat = host_feat & (1u << 5);   /* VIRTIO_NET_F_MAC */
    outl(vn.io_base + VIRTIO_REG_GUEST_FEATURES, guest_feat);

    /* 3. Read MAC from device-specific region (offset 0x14 in legacy I/O). */
    for (int i = 0; i < 6; i++) {
        vn.netdev.hwaddr[i] = inb(vn.io_base + 0x14 + i);
    }

    /* 4. Set up queues. */
    /* VNET-01: a queue we cannot back exactly must abort the attach, not be
     * silently mismatched against the device's own ring geometry. */
    if (vnet_queue_init(&vn.rxq, 0) != 0) return -1;
    if (vnet_queue_init(&vn.txq, 1) != 0) return -1;

    /* 5. DRIVER_OK */
    outb(vn.io_base + VIRTIO_REG_DEVICE_STATUS, 7);  /* ACK|DRIVER|DRIVER_OK */

    /* 6. Hook the IRQ. */
    /* VNET-04: virtio-net commonly shares its PCI INTx line with
     * virtio-blk / AHCI.  Without IRQF_SHARED one of the two registrations
     * is refused with -EBUSY -- either this NIC gets no handler and the
     * interface is dead, or the sibling loses its own -- and a level-
     * triggered line with no servicer re-delivers forever. */
    if (vn.irq) {
        int rc = request_irq(vn.irq, vnet_irq, IRQF_SHARED, "virtio-net", &vn);
        if (rc != 0) {
            kprint("virtio-net: could not install IRQ handler\n");
            return -1;
        }
    }

    /* 7. Publish empty RX buffers. */
    vnet_rx_publish_all();

    /* 8. Register the netdev. */
    strlcpy(vn.netdev.name, "eth0", NETDEV_NAME_MAX);
    vn.netdev.mtu = 1500;
    vn.netdev.flags = NETDEV_IFF_UP | NETDEV_IFF_BROADCAST | NETDEV_IFF_RUNNING;
    vn.netdev.ops = &vnet_ops;
    vn.netdev.driver_data = &vn;
    netdev_register(&vn.netdev);
    vn.registered = 1;
    return 0;
}
