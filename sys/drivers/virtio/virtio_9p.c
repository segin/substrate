#include "virtio.h"
#include "../../arch/i386/io.h"
#include <kern/console.h>
#include "../../arch/i386/pmm.h"
#include <kern/panic.h>
#include <string.h>
#include <stdio.h>

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
    
    void *q_mem = pmm_alloc_block();
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
    
    outl(v9p.io_base + VIRTIO_REG_QUEUE_ADDR, (uint32_t)q_mem / 4096);
    
    outb(v9p.io_base + VIRTIO_REG_DEVICE_STATUS, 
         VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);
         
    kprint("VirtIO-9P Initialized.\n");
}

int virtio_9p_send(void *out_buf, uint32_t out_len, void *in_buf, uint32_t in_len) {
    if (!v9p.io_base) return -1;
    
    // Simplest sync implementation
    // ID 0: OUT (Request)
    // ID 1: IN (Response)
    
    int id0 = 0;
    int id1 = 1;
    
    v9p.desc[id0].addr = (uint64_t)(uint32_t)out_buf;
    v9p.desc[id0].len = out_len;
    v9p.desc[id0].flags = VRING_DESC_F_NEXT;
    v9p.desc[id0].next = id1;
    
    v9p.desc[id1].addr = (uint64_t)(uint32_t)in_buf;
    v9p.desc[id1].len = in_len;
    v9p.desc[id1].flags = VRING_DESC_F_WRITE;
    v9p.desc[id1].next = 0;
    
    v9p.avail->ring[v9p.avail->idx % v9p.q_size] = id0;
    __asm__ volatile("lock addw $1, %0" : "+m"(v9p.avail->idx));
    
    outw(v9p.io_base + VIRTIO_REG_QUEUE_NOTIFY, 0);
    
    while (v9p.last_used_idx == v9p.used->idx) {
        __asm__ volatile("pause");
    }
    
    v9p.last_used_idx++;
    return 0;
}
