#include <drivers/virtio/virtio.h>
#include <arch/i386/cpu.h>
#include <arch/x86-common/io.h>
#include <kern/geom/geom.h>
#include <kern/console.h>
#include <arch/i386/pmm.h>
#include <arch/i386/pmap.h>
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

struct virtio_blk_req_hdr {
    uint32_t type;
    uint32_t ioprio;
    uint64_t sector;
}; // Followed by data, then status byte

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

static int virtio_blk_read_sectors(geom_disk_t *disk, uint64_t lba, size_t count, void *buf);

static geom_disk_t vblk_disk = {
    .name = "vda",
    .sector_size = 512,
    .read = virtio_blk_read_sectors
};

void virtio_blk_setup(uint8_t bus, uint8_t slot, uint8_t func) {
    vblk.io_base = virtio_get_io_base(bus, slot, func);
    if (!vblk.io_base) {
        kprint("VirtIO-Blk: No IO Base declared.\n");
        return;
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

    // Write PFN to Queue Address - MUST be physical address!
    // pmm_alloc_contiguous returns virtual, convert to physical.
    uint32_t q_phys = (uint32_t)(uintptr_t)q_mem - 0xC0000000;
    outl(vblk.io_base + VIRTIO_REG_QUEUE_ADDR, q_phys / 4096);

    // 5. Driver OK
    outb(vblk.io_base + VIRTIO_REG_DEVICE_STATUS,
         VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);

    kprint("VirtIO-Blk Initialized.\n");

    geom_register_disk(&vblk_disk);
}

// Synchronous Read
static int virtio_blk_read_sectors(geom_disk_t *disk, uint64_t lba, size_t count, void *buf) {
    (void)disk;
    
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

    // 1. Setup Descriptors
    // We need 3 descriptors: Header (OUT), Data (IN), Status (IN).
    // Simple implementation: Use first 3 descriptors of the ring, assuming single-threaded, sync.
    
    struct virtio_blk_req_hdr hdr;
    hdr.type = VIRTIO_BLK_T_IN;
    hdr.ioprio = 0;
    hdr.sector = lba;
    
    uint8_t status = 0;
    
    int id0 = 0;
    int id1 = 1;
    int id2 = 2;

    /* Own the shared ring for the entire request/poll cycle. */
    vblk_lock();

    vblk.desc[id0].addr = (uint64_t)pmap_extract(pmap_kernel(), (uintptr_t)&hdr);
    vblk.desc[id0].len = sizeof(hdr);
    vblk.desc[id0].flags = VRING_DESC_F_NEXT;
    vblk.desc[id0].next = id1;
    
    vblk.desc[id1].addr = (uint64_t)pmap_extract(pmap_kernel(), (uintptr_t)buf);
    vblk.desc[id1].len = 512 * count;
    vblk.desc[id1].flags = VRING_DESC_F_NEXT | VRING_DESC_F_WRITE;
    vblk.desc[id1].next = id2;
    
    vblk.desc[id2].addr = (uint64_t)pmap_extract(pmap_kernel(), (uintptr_t)&status);
    vblk.desc[id2].len = 1;
    vblk.desc[id2].flags = VRING_DESC_F_WRITE;
    vblk.desc[id2].next = 0;
    
    // 2. Put in Avail Ring.
    //
    // Store-release between the descriptor writes above and the avail
    // index bump: on a weakly-ordered host (or with compiler reordering)
    // the device could otherwise observe a bumped idx pointing at a
    // half-written descriptor chain.
    vblk.avail->ring[vblk.avail->idx % vblk.q_size] = id0;
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

    vblk_unlock();

    return (status == 0) ? 0 : -1;
}
