#include <stdio.h>
#include <string.h>

#include <arch/i386/pmm.h>
#include <arch/x86-common/io.h>
#include <drivers/virtio/virtio.h>
#include <kern/console.h>
#include <kern/panic.h>
#include <stdint.h>
#include <sys/lock.h>

/* Kernel direct-map base.  The descriptor addresses below are va - this. */
#define VIRTIO_9P_KERN_BASE  0xC0000000U

/*
 * [9P-25] Serialises the whole request/reply exchange.
 *
 * This function hardcodes descriptors 0 and 1 for every request and then
 * waits on the used ring.  With two threads in here at once, the second
 * overwrites the first's descriptors while the device is reading them, both
 * wait on the same ring, and whichever wakes first consumes the other's
 * reply -- into the wrong buffer, with no tag to notice by (every request
 * also used tag 0).  Silent cross-talk between readers, not a crash.
 *
 * A mutex, not a spinlock: the wait below can spin for a long time and the
 * 9P path is only ever entered from process context.
 */
static mutex_t v9p_lock;   /* mutex_init()ed in virtio_9p_setup */

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
    /* [9P-25] Before anything can reach virtio_9p_send(). */
    mutex_init(&v9p_lock, "virtio9p");

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
    uint32_t avail_ring_end;
    uint32_t used_ring_offset;
    uint32_t ring_bytes;
    uint32_t ring_pages;
    void *q_mem;
    uint32_t q_phys;

    if (q_size == 0) {
        kprint("VirtIO-9P: device reports a zero-length queue\n");
        goto disable;
    }
    v9p.q_size = q_size;

    /*
     * [9P-25] The ring was allocated as ONE 4 KiB page and the layout then
     * checked against that fixed size:
     *
     *     if (used_ring_offset + 6 + 8 * q_size > 4096) {
     *          kprint("VirtIO-9P: Queue too large!\n");
     *          return;
     *     }
     *
     * QEMU's virtio-9p offers a 128-entry queue, which needs 5126 bytes, so
     * that branch is what actually happens on a real device -- and it
     * `return`ed having ALREADY set v9p.io_base and v9p.desc/v9p.avail while
     * leaving v9p.used NULL and never programming QUEUE_ADDR.  The driver
     * looked initialised to virtio_9p_send(), which then dereferenced
     * v9p.used -- a NULL read at offset 2 -- on the first mount attempt.
     *
     * Size the allocation to the ring the device actually offers, and if
     * that cannot be satisfied, disable the device rather than leave it
     * half-armed.
     */
    avail_ring_end   = 16u * q_size + 6u + 2u * q_size;
    used_ring_offset = (avail_ring_end + 4095u) & ~4095u;
    ring_bytes       = used_ring_offset + 6u + 8u * q_size;
    ring_pages       = (ring_bytes + 4095u) / 4096u;

    q_mem = pmm_alloc_contiguous(ring_pages);   // Returns virtual address
    if (!q_mem) {
        kprintf("VirtIO-9P: could not allocate %u pages for a %u-entry "
                "queue\n", (unsigned)ring_pages, (unsigned)q_size);
        goto disable;
    }
    memset(q_mem, 0, ring_pages * 4096u);

    v9p.desc_page = q_mem;
    v9p.desc = (struct vring_desc *)q_mem;
    v9p.avail = (struct vring_avail *)((char*)q_mem + 16 * q_size);
    v9p.used = (struct vring_used *)((char*)q_mem + used_ring_offset);

    // Write PFN - MUST be physical address!
    q_phys = (uint32_t)(uintptr_t)q_mem - 0xC0000000;
    outl(v9p.io_base + VIRTIO_REG_QUEUE_ADDR, q_phys / 4096);
    
    outb(v9p.io_base + VIRTIO_REG_DEVICE_STATUS, 
         VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);
         
    kprint("VirtIO-9P Initialized.\n");
    return;

disable:
    /*
     * [9P-25] Every failure path here used to be a bare `return` that left
     * io_base set, so virtio_9p_send() happily ran against a queue that was
     * never programmed.  Clear the state so it refuses instead.
     */
    v9p.io_base = 0;
    v9p.desc = NULL;
    v9p.avail = NULL;
    v9p.used = NULL;
    v9p.desc_page = NULL;
    v9p.q_size = 0;
    kprint("VirtIO-9P: disabled.\n");
}

#define VIRTIO_9P_WAIT_SPINS  200000000u

/*
 * The descriptor addresses below are computed as va - KERN_BASE, so a buffer
 * outside the kernel direct map yields a garbage physical address and hands
 * the device an arbitrary page to read or WRITE.  Callers pass kernel stack
 * and kmalloc'd buffers today; check rather than assume.
 */
static int v9p_direct_mapped(const void *p, uint32_t len) {
    uintptr_t va = (uintptr_t)p;

    if (va < VIRTIO_9P_KERN_BASE)
        return 0;
    /* No wrap off the top of the address space. */
    if ((uintptr_t)len > (uintptr_t)0 - va)
        return 0;
    return 1;
}

int virtio_9p_send(void *out_buf, uint32_t out_len, void *in_buf, uint32_t in_len) {
    uint32_t used_len;
    uint32_t spins;

    if (!v9p.io_base || !v9p.desc || !v9p.avail || !v9p.used) return -1;
    if (!out_buf || !in_buf || out_len == 0) return -1;

    if (!v9p_direct_mapped(out_buf, out_len) ||
        !v9p_direct_mapped(in_buf, in_len)) {
        kprint("virtio-9p: refusing a buffer outside the kernel direct map\n");
        return -1;
    }

    // Simplest sync implementation
    // ID 0: OUT (Request)
    // ID 1: IN (Response)

    int id0 = 0;
    int id1 = 1;

    mutex_lock(&v9p_lock);

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
    spins = 0;
    while (v9p.last_used_idx == v9p.used->idx) {
        if (++spins > VIRTIO_9P_WAIT_SPINS) {
            mutex_unlock(&v9p_lock);
            return -1;
        }
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
    used_len = v9p.used->ring[v9p.last_used_idx % v9p.q_size].len;
    v9p.last_used_idx++;

    /* The device must never claim to have written more than we offered. */
    if (used_len > in_len)
        used_len = in_len;

    mutex_unlock(&v9p_lock);
    return (int)used_len;
}
