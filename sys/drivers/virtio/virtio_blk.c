#include <drivers/virtio/virtio.h>
#include <arch/x86-common/include/io.h>
#include <kern/geom/geom.h>
#include <kern/console.h>
#include <arch/i386/pmm.h>
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
} vblk;

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
    vblk.q_size = q_size;
    
    // Allocate Queue Memory (must be page aligned, physically contiguous)
    // Ring size = Desc(16*N) + Avail(6+2*N) + Padding + Used(6+8*N)
    // 4096 is enough for small N (e.g. 64 or 128)
    void *q_mem = pmm_alloc_block();  // Returns virtual address
    if (!q_mem) {
        kprint("VirtIO-Blk: Failed to allocate queue memory!\n");
        return;
    }
    memset(q_mem, 0, 4096);
    
    vblk.desc_page = q_mem;
    vblk.desc = (struct vring_desc *)q_mem;
    vblk.avail = (struct vring_avail *)((char*)q_mem + 16 * q_size);
    
    uint32_t avail_ring_end = 16 * q_size + 6 + 2 * q_size;
    uint32_t used_ring_offset = (avail_ring_end + 4095) & ~4095;
    
    // We need more than 1 page if used_ring_offset >= 4096
    if (used_ring_offset + 6 + 8 * q_size > 4096) {
         // Simplify: panic if queue too large for single page
         kprint("VirtIO-Blk: Queue too large for single page support!\n");
         return;
    }
    
    vblk.used = (struct vring_used *)((char*)q_mem + used_ring_offset);
    
    // Write PFN to Queue Address - MUST be physical address!
    // pmm_alloc_block returns virtual, convert to physical
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
    
    uint64_t tsc;
    __asm__ volatile("rdtsc" : "=A"(tsc));
    
    entropy_data.lba = lba;
    entropy_data.count = count;
    entropy_data.tsc = tsc;
    
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
    
    vblk.desc[id0].addr = (uint64_t)(uint32_t)&hdr;
    vblk.desc[id0].len = sizeof(hdr);
    vblk.desc[id0].flags = VRING_DESC_F_NEXT;
    vblk.desc[id0].next = id1;
    
    vblk.desc[id1].addr = (uint64_t)(uint32_t)buf;
    vblk.desc[id1].len = 512 * count;
    vblk.desc[id1].flags = VRING_DESC_F_NEXT | VRING_DESC_F_WRITE;
    vblk.desc[id1].next = id2;
    
    vblk.desc[id2].addr = (uint64_t)(uint32_t)&status;
    vblk.desc[id2].len = 1;
    vblk.desc[id2].flags = VRING_DESC_F_WRITE;
    vblk.desc[id2].next = 0;
    
    // 2. Put in Avail Ring
    vblk.avail->ring[vblk.avail->idx % vblk.q_size] = id0;
    __asm__ volatile("lock addw $1, %0" : "+m"(vblk.avail->idx)); // Memory Barrier implicit
    
    // 3. Notify
    outw(vblk.io_base + VIRTIO_REG_QUEUE_NOTIFY, 0); // Queue 0
    
    // 4. Poll Used Ring
    while (vblk.last_used_idx == vblk.used->idx) {
        // Spin
        __asm__ volatile("pause");
    }
    
    vblk.last_used_idx++;
    
    return (status == 0) ? 0 : -1;
}
