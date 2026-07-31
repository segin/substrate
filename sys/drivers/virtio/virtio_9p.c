#include <stdio.h>
#include <string.h>

#include <arch/i386/pmm.h>
#include <arch/x86-common/io.h>
#include <drivers/virtio/virtio.h>
#include <kern/console.h>
#include <kern/panic.h>

static struct {
    uint16_t io_base;
    uint16_t q_size;
    void *desc_page;
    struct vring_desc *desc;
    struct vring_avail *avail;
    struct vring_used *used;
    uint16_t last_used_idx;
    char mount_tag[64];
} v9p;

void virtio_9p_setup(uint8_t bus, uint8_t slot, uint8_t func) {
    v9p.io_base = virtio_get_io_base(bus, slot, func);
    if (!v9p.io_base) return;
    
    // Reset & Ack
    outb(v9p.io_base + VIRTIO_REG_DEVICE_STATUS, 0);
    outb(v9p.io_base + VIRTIO_REG_DEVICE_STATUS, 
         VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
         
    // Read Mount Tag (Config space 0)
    // Legacy 9P config: len (2 bytes), then string.
    // IO address for config is VIRTIO_REG_ISR_STATUS + 1 (20 decimal / 0x14)
    // Offset 20 is "MSI-X" in PCIe but in Legacy PIO it's config.
    // config starts at 20.
    
    uint16_t tag_len = inw(v9p.io_base + 20);
    if (tag_len > 63) tag_len = 63;
    for (int i = 0; i < tag_len; i++) {
        v9p.mount_tag[i] = inb(v9p.io_base + 22 + i);
    }
    v9p.mount_tag[tag_len] = 0;
    
    kprint("VirtIO-9P: Mount Tag: ");
    kprint(v9p.mount_tag);
    kprint("\n");
    
    // Setup Queue 0
    uint16_t q_size = inw(v9p.io_base + VIRTIO_REG_QUEUE_SIZE);
    v9p.q_size = q_size;
    
    void *q_mem = pmm_alloc_block();  // Returns virtual address
    if (!q_mem) {
        kprint("VirtIO-9P: Failed to allocate queue memory!\n");
        return;
    }
    memset(q_mem, 0, 4096);
    
    v9p.desc_page = q_mem;
    v9p.desc = (struct vring_desc *)q_mem;
    v9p.avail = (struct vring_avail *)((char*)q_mem + 16 * q_size);
    
    uint32_t avail_ring_end = 16 * q_size + 6 + 2 * q_size;
    uint32_t used_ring_offset = (avail_ring_end + 4095) & ~4095;
    
    if (used_ring_offset + 6 + 8 * q_size > 4096) {
         kprint("VirtIO-9P: Queue too large!\n");
         return;
    }
    
    v9p.used = (struct vring_used *)((char*)q_mem + used_ring_offset);
    
    // Write PFN - MUST be physical address!
    uint32_t q_phys = (uint32_t)(uintptr_t)q_mem - 0xC0000000;
    outl(v9p.io_base + VIRTIO_REG_QUEUE_ADDR, q_phys / 4096);
    
    outb(v9p.io_base + VIRTIO_REG_DEVICE_STATUS, 
         VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);
         
    kprint("VirtIO-9P Initialized.\n");
}

#define VIRTIO_9P_WAIT_SPINS  200000000u

int virtio_9p_send(void *out_buf, uint32_t out_len, void *in_buf, uint32_t in_len) {
    if (!v9p.io_base) return -1;
    
    // Simplest sync implementation
    // ID 0: OUT (Request)
    // ID 1: IN (Response)
    
    int id0 = 0;
    int id1 = 1;
    
    v9p.desc[id0].addr = (uint64_t)((uint32_t)(uintptr_t)out_buf - 0xC0000000);
    v9p.desc[id0].len = out_len;
    v9p.desc[id0].flags = VRING_DESC_F_NEXT;
    v9p.desc[id0].next = id1;
    
    v9p.desc[id1].addr = (uint64_t)((uint32_t)(uintptr_t)in_buf - 0xC0000000);
    v9p.desc[id1].len = in_len;
    v9p.desc[id1].flags = VRING_DESC_F_WRITE;
    v9p.desc[id1].next = 0;
    
    v9p.avail->ring[v9p.avail->idx % v9p.q_size] = id0;
    __asm__ volatile("lock addw $1, %0" : "+m"(v9p.avail->idx));
    
    outw(v9p.io_base + VIRTIO_REG_QUEUE_NOTIFY, 0);
    
    /*
     * [9P-25] The wait used to be an unbounded `while (...) pause;`.  A server
     * that never completes the request wedged the calling thread forever with
     * no way out.  Bound it; a 9P server that has not answered in this many
     * spins is not going to.
     */
    uint32_t spins = 0;
    while (v9p.last_used_idx == v9p.used->idx) {
        if (++spins > VIRTIO_9P_WAIT_SPINS)
            return -1;
        __asm__ volatile("pause");
    }

    /*
     * [9P-09] The used-ring element carries how many bytes the device
     * actually wrote into our IN buffer.  This was thrown away and the
     * function returned a bare 0, so every caller had to take the reply
     * body's own self-declared length on trust -- letting a malicious or
     * buggy server claim a count far larger than it wrote and walk the
     * caller off the end of freshly-kmalloc'd (uninitialised) heap.
     * Return the real length so callers can bound their parse by it.
     */
    uint32_t used_len = v9p.used->ring[v9p.last_used_idx % v9p.q_size].len;
    v9p.last_used_idx++;

    /* The device must never claim to have written more than we offered. */
    if (used_len > in_len)
        used_len = in_len;

    return (int)used_len;
}
