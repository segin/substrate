#include <drivers/virtio/virtio.h>
#include <arch/i386/cpu.h>
#include <arch/x86-common/io.h>
#include <kern/geom/geom.h>
#include <drivers/storage/blkdev.h>
#include <kern/console.h>
#include <arch/i386/pmm.h>
#include <arch/i386/pmap.h>
#include <kern/pci.h>
#include <kern/panic.h>
#include <string.h>
#include <stdio.h>
#include <sys/random.h>

#define VIRTIO_BLK_F_SIZE_MAX   1
#define VIRTIO_BLK_F_SEG_MAX    2
#define VIRTIO_BLK_F_GEOMETRY   4
#define VIRTIO_BLK_F_RO         5
#define VIRTIO_BLK_F_BLK_SIZE   6
#define VIRTIO_BLK_F_FLUSH      9

#define VIRTIO_BLK_T_IN         0
#define VIRTIO_BLK_T_OUT        1
#define VIRTIO_BLK_T_FLUSH      4

/*
 * Legacy virtio-pci puts the device-specific config block immediately after
 * the 20-byte common header when MSI-X is disabled (at 0x18 when it is
 * enabled -- we never enable it).  virtio-blk's config starts with a 64-bit
 * capacity in 512-byte sectors.
 */
#define VIRTIO_BLK_CFG_CAPACITY 0x14

/* Kernel direct map: every page the PMM hands out is reachable at
 * phys + 0xC0000000, so a physical address is a plain subtraction.  Same
 * constant virtio_net and virtio_scsi use. */
#define VIRTIO_BLK_KERNEL_BASE  0xC0000000u

/* Size of the driver-owned DMA bounce region's data area.  Requests larger
 * than this are split; 64 KiB comfortably covers the block layer's
 * read-ahead window while keeping the allocation small. */
#define VIRTIO_BLK_BOUNCE_BYTES   (64u * 1024u)
#define VIRTIO_BLK_BOUNCE_SECTORS (VIRTIO_BLK_BOUNCE_BYTES / 512u)

struct virtio_blk_req_hdr {
    uint32_t type;
    uint32_t ioprio;
    uint64_t sector;
}; // Followed by data, then status byte

/*
 * One physically-contiguous region holding everything the device touches
 * for a request.  Keeping the header and status byte here rather than on
 * the kernel stack matters: stack addresses would need a page-table walk to
 * translate, and pmap_extract() cannot do that for the kernel pmap while a
 * user pmap is loaded in CR3.
 */
struct virtio_blk_dma {
    struct virtio_blk_req_hdr hdr;
    uint8_t status;
    uint8_t pad[4096 - sizeof(struct virtio_blk_req_hdr) - 1];
    uint8_t data[VIRTIO_BLK_BOUNCE_BYTES];
};

// Driver State
static struct {
    uint16_t io_base;
    uint32_t capacity; // In sectors (if sector size is 512)
    
    // VirtQueue 0
    uint16_t q_size;
    void *desc_page;
    struct vring_desc *desc;
    struct vring_avail *avail;
    struct vring_used *used;
    uint16_t last_used_idx;

    /* Driver-owned DMA staging region (direct-mapped, physically
     * contiguous): request header, status byte and bounce buffer. */
    struct virtio_blk_dma *dma;

    /* Serializes the whole request/poll cycle: descriptors 0-2, the avail-ring
     * publish and the shared last_used_idx poll are a single global resource,
     * so two concurrent callers (SMP, or two threads that both miss the block
     * cache) would otherwise clobber each other's descriptor chain / status
     * byte and mis-attribute completions.  Mirrors virtio_scsi's per-queue
     * busy flag. */
    volatile uint32_t io_busy;
} vblk;

static inline void vblk_lock(void) {
    while (__sync_lock_test_and_set(&vblk.io_busy, 1) != 0)
        __asm__ volatile("pause");
}

static inline void vblk_unlock(void) {
    __sync_lock_release(&vblk.io_busy);
}

/* Physical address of a direct-mapped kernel pointer.  Only valid for
 * memory obtained from the PMM (the ring and the DMA region), which is
 * exactly what this driver hands to the device. */
static inline uint64_t vblk_phys(const void *p) {
    return (uint64_t)((uintptr_t)p - VIRTIO_BLK_KERNEL_BASE);
}

static int vblk_bdev_read(blkdev_t *dev, uint64_t sector, uint32_t count, void *buffer);
static int vblk_bdev_write(blkdev_t *dev, uint64_t sector, uint32_t count,
                           const void *buffer);

/*
 * Registered with blkdev_register_disk(), not geom_register_disk().  The
 * latter only *scans* a disk for partition tables -- it creates no
 * /dev/storage node -- so the previous registration left virtio-blk with no
 * device node at all and it could never be a root device.  Every other
 * storage driver (ahci, ide, scsi, ramdisk) goes through
 * blkdev_register_disk(), which registers /dev/storage/<name> and then hands
 * the disk to GEOM for partition scanning.
 */
static blkdev_t vblk_bdev;

void virtio_blk_setup(uint8_t bus, uint8_t slot, uint8_t func) {
    vblk.io_base = virtio_get_io_base(bus, slot, func);
    if (!vblk.io_base) {
        kprint("VirtIO-Blk: No IO Base declared.\n");
        return;
    }
    
    /* Enable PCI I/O decoding + bus-mastering.  Bus-master (bit 2) is
     * mandatory and its absence is silent: config-space reads are plain PIO
     * and work regardless, so the device reports its capacity and accepts the
     * queue address and the notify -- but it can never DMA, so it never
     * fetches the descriptor table and never writes the used ring, and the
     * completion poll below spins forever.  That is exactly how this driver
     * hung on its very first read.  virtio_net sets the same bits. */
    {
        uint32_t cmd = pci_read(bus, slot, func, PCI_CONFIG_COMMAND);
        pci_write(bus, slot, func, PCI_CONFIG_COMMAND,
                  cmd | PCI_COMMAND_IO | PCI_COMMAND_MASTER);
    }

    // 1. Reset
    outb(vblk.io_base + VIRTIO_REG_DEVICE_STATUS, 0);
    
    // 2. ACK
    outb(vblk.io_base + VIRTIO_REG_DEVICE_STATUS, 
         VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
         
    // 3. Negotiate Features (Skip for now, accept default)
    
    // 4. Setup Queue 0
    uint16_t q_size = inw(vblk.io_base + VIRTIO_REG_QUEUE_SIZE);
    if (q_size == 0) {
        kprint("VirtIO-Blk: Device reports no queue 0.\n");
        outb(vblk.io_base + VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_FAILED);
        return;
    }
    vblk.q_size = q_size;

    /*
     * Legacy virtio split-ring layout in ONE physically-contiguous,
     * page-aligned region:
     *   desc[q_size]        16 * q_size
     *   avail               6 + 2 * q_size
     *   <pad to VIRTIO_PCI_VRING_ALIGN>
     *   used                6 + 8 * q_size
     * The device derives all three rings from the single QUEUE_ADDR PFN
     * using exactly this layout, so we size + align identically and
     * allocate however many pages the ring actually needs.  The old code
     * pinned the region to a single 4 KiB page and then rejected any queue
     * whose used ring spilled past it -- which is EVERY real q_size (the
     * desc+avail of even q_size 64 already rounds the used ring to offset
     * 4096), so the driver never reached DRIVER_OK.
     */
    uint32_t desc_avail_end = 16u * q_size + 6u + 2u * q_size;
    uint32_t used_ring_offset =
        (desc_avail_end + (VIRTIO_PCI_VRING_ALIGN - 1u)) &
        ~(VIRTIO_PCI_VRING_ALIGN - 1u);
    uint32_t ring_bytes = used_ring_offset + 6u + 8u * q_size;
    size_t   q_pages = (ring_bytes + 4095u) / 4096u;

    // Allocate Queue Memory (page aligned, physically contiguous).
    void *q_mem = pmm_alloc_contiguous(q_pages);  // Returns virtual address
    if (!q_mem) {
        kprint("VirtIO-Blk: Failed to allocate queue memory!\n");
        outb(vblk.io_base + VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_FAILED);
        return;
    }
    memset(q_mem, 0, q_pages * 4096u);

    vblk.desc_page = q_mem;
    vblk.desc  = (struct vring_desc *)q_mem;
    vblk.avail = (struct vring_avail *)((char*)q_mem + 16 * q_size);
    vblk.used  = (struct vring_used *)((char*)q_mem + used_ring_offset);

    /*
     * This driver polls the used ring and registers no IRQ handler, so tell
     * the device never to raise a completion interrupt.  virtio-pci INTx is
     * level-triggered: a single unacknowledged completion interrupt leaves
     * the line asserted and the PIC re-delivers it forever, wedging the
     * machine immediately after the first successful transfer.
     */
    vblk.avail->flags = VRING_AVAIL_F_NO_INTERRUPT;

    /*
     * DMA staging region: request header, status byte and the bounce
     * buffer, all in one physically-contiguous direct-mapped allocation so
     * their physical addresses need no page-table walk (see virtio_blk_rw).
     */
    {
        size_t dma_pages = (sizeof(struct virtio_blk_dma) + 4095u) / 4096u;
        void *dma_mem = pmm_alloc_contiguous(dma_pages);
        if (!dma_mem) {
            kprint("VirtIO-Blk: Failed to allocate DMA buffer!\n");
            outb(vblk.io_base + VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_FAILED);
            return;
        }
        memset(dma_mem, 0, dma_pages * 4096u);
        vblk.dma = (struct virtio_blk_dma *)dma_mem;
    }

    // Write PFN to Queue Address - MUST be physical address!
    // pmm_alloc_contiguous returns virtual, convert to physical.
    uint32_t q_phys = (uint32_t)vblk_phys(q_mem);
    outl(vblk.io_base + VIRTIO_REG_QUEUE_ADDR, q_phys / 4096);

    // 5. Driver OK
    outb(vblk.io_base + VIRTIO_REG_DEVICE_STATUS,
         VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);

    /*
     * Capacity, in 512-byte sectors, from the device-specific config block.
     * Without this total_sectors stayed 0, which alone is fatal for a root
     * device: geom_add_partition() rejects a zero-size extent, so even a
     * partitioned disk produced no usable block device.
     */
    {
        uint64_t capacity =
            (uint64_t)inl(vblk.io_base + VIRTIO_BLK_CFG_CAPACITY) |
            ((uint64_t)inl(vblk.io_base + VIRTIO_BLK_CFG_CAPACITY + 4) << 32);
        uint32_t host_features = inl(vblk.io_base + VIRTIO_REG_HOST_FEATURES);
        char msg[80];

        if (capacity == 0) {
            kprint("VirtIO-Blk: device reports zero capacity.\n");
            outb(vblk.io_base + VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_FAILED);
            return;
        }
        vblk.capacity = (uint32_t)capacity;

        memset(&vblk_bdev, 0, sizeof(vblk_bdev));
        /* Substrate names storage devices <type><instance> (ide0, sata0,
         * scsi0), not by the Linux vd* convention. */
        strlcpy(vblk_bdev.name, "virtio0", sizeof(vblk_bdev.name));
        vblk_bdev.sector_size = 512;
        vblk_bdev.total_sectors = capacity;
        vblk_bdev.read = vblk_bdev_read;
        /* Honour a read-only device (VIRTIO_BLK_F_RO): leaving .write set
         * would let the block layer issue writes the device rejects. */
        vblk_bdev.write = (host_features & (1u << VIRTIO_BLK_F_RO))
                              ? NULL : vblk_bdev_write;

        snprintf(msg, sizeof(msg), "VirtIO-Blk: virtio0, %u sectors (%u MiB)%s\n",
                 (unsigned)capacity, (unsigned)(capacity / 2048u),
                 vblk_bdev.write ? "" : ", read-only");
        kprint(msg);

        blkdev_register_disk(&vblk_bdev);
    }
}

/*
 * Issue one already-bounced request: three descriptors (header, data, status)
 * all pointing into the driver's own DMA region.  Caller holds vblk_lock and
 * has staged write data into the bounce buffer.
 */
static int virtio_blk_submit(uint64_t lba, uint32_t count, int write) {
    struct virtio_blk_req_hdr *hdr = &vblk.dma->hdr;
    volatile uint8_t *status = &vblk.dma->status;

    hdr->type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    hdr->ioprio = 0;
    hdr->sector = lba;
    *status = 0xFF;

    /* desc[0]: request header, device-readable. */
    vblk.desc[0].addr = vblk_phys(hdr);
    vblk.desc[0].len = sizeof(*hdr);
    vblk.desc[0].flags = VRING_DESC_F_NEXT;
    vblk.desc[0].next = 1;

    /* desc[1]: the data.  F_WRITE means "device writes here", so it is set
     * for reads and clear for writes. */
    vblk.desc[1].addr = vblk_phys(vblk.dma->data);
    vblk.desc[1].len = 512u * count;
    vblk.desc[1].flags = VRING_DESC_F_NEXT |
                         (write ? 0u : (uint16_t)VRING_DESC_F_WRITE);
    vblk.desc[1].next = 2;

    /* desc[2]: status byte, device-writable. */
    vblk.desc[2].addr = vblk_phys((void *)status);
    vblk.desc[2].len = 1;
    vblk.desc[2].flags = VRING_DESC_F_WRITE;
    vblk.desc[2].next = 0;

    // 2. Put in Avail Ring.
    //
    // Store-release between the descriptor writes above and the avail
    // index bump: on a weakly-ordered host (or with compiler reordering)
    // the device could otherwise observe a bumped idx pointing at a
    // half-written descriptor chain.
    vblk.avail->ring[vblk.avail->idx % vblk.q_size] = 0;   /* chain head */
    __asm__ volatile("sfence" ::: "memory");
    __asm__ volatile("lock addw $1, %0" : "+m"(vblk.avail->idx));

    // 3. Notify (mfence so the bumped idx is observed before the IO write)
    __asm__ volatile("mfence" ::: "memory");
    outw(vblk.io_base + VIRTIO_REG_QUEUE_NOTIFY, 0); // Queue 0

    // 4. Poll Used Ring with an acquire barrier so the load of used->idx
    //    isn't hoisted above the ring-reads we're about to do.
    while (vblk.last_used_idx == vblk.used->idx) {
        __asm__ volatile("pause");
    }
    __asm__ volatile("lfence" ::: "memory");

    vblk.last_used_idx++;

    /* Belt and braces alongside VRING_AVAIL_F_NO_INTERRUPT: reading the ISR
     * status register clears it and deasserts INTx, so a device that ignores
     * the no-interrupt hint still cannot leave the (level-triggered) line
     * latched high with no handler to service it. */
    (void)inb(vblk.io_base + VIRTIO_REG_ISR_STATUS);

    return (*status == 0) ? 0 : -1;
}

/*
 * Synchronous read or write of `count` 512-byte sectors at `lba`.
 *
 * Transfers stage through the driver's own physically-contiguous DMA region
 * rather than DMAing straight into the caller's buffer, which is what the
 * AHCI driver does and for the same reason: there is no scatter-gather
 * mapping layer, and a caller buffer is only guaranteed to be *virtually*
 * contiguous.  Bouncing also sidesteps a trap that made this driver
 * unusable as a root device -- pmap_extract() returns 0 unless the pmap
 * asked about is the one currently in CR3, so translating kernel addresses
 * via pmap_extract(pmap_kernel(), ...) silently failed the moment a user
 * process's pmap was active.  Reads during early boot worked; the first one
 * issued on behalf of a process (execve reading the ELF interpreter) did
 * not.  The DMA region is allocated from the direct map, so its physical
 * address is a plain subtraction with no page-table walk at all.
 */
static int virtio_blk_rw(uint64_t lba, uint32_t count, void *buf, int write) {
    uint8_t *p = (uint8_t *)buf;
    int rc = 0;

    if (!buf || count == 0) {
        return -1;
    }
    if (!vblk.dma) {
        return -1;
    }

    // Harvest entropy from disk I/O request
    struct {
        uint64_t lba;
        uint32_t count;
        uint64_t tsc;
    } __attribute__((packed)) entropy_data;

    entropy_data.lba = lba;
    entropy_data.count = count;
    entropy_data.tsc = i386_cpu_cycle_counter();

    random_harvest_fast(&entropy_data, sizeof(entropy_data));

    /* Own the shared ring and bounce buffer for the whole transfer. */
    vblk_lock();

    while (count > 0) {
        uint32_t chunk = (count > VIRTIO_BLK_BOUNCE_SECTORS)
                             ? VIRTIO_BLK_BOUNCE_SECTORS : count;

        if (write) {
            memcpy(vblk.dma->data, p, 512u * chunk);
        }
        rc = virtio_blk_submit(lba, chunk, write);
        if (rc != 0) {
            break;
        }
        if (!write) {
            memcpy(p, vblk.dma->data, 512u * chunk);
        }

        lba += chunk;
        p += 512u * chunk;
        count -= chunk;
    }

    vblk_unlock();
    return rc;
}

static int vblk_bdev_read(blkdev_t *dev, uint64_t sector, uint32_t count,
                          void *buffer) {
    (void)dev;
    return virtio_blk_rw(sector, count, buffer, 0);
}

static int vblk_bdev_write(blkdev_t *dev, uint64_t sector, uint32_t count,
                           const void *buffer) {
    (void)dev;
    /* The device only reads this buffer (VIRTIO_BLK_T_OUT); casting away
     * const is confined to handing the physical address to the ring. */
    return virtio_blk_rw(sector, count, (void *)(uintptr_t)buffer, 1);
}
