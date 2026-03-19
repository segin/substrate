#include <drivers/virtio/virtio.h>

#include <arch/i386/pmm.h>
#include <arch/x86-common/io.h>
#include <kern/console.h>
#include <string.h>

#define VIRTIO_GPU_CTRLQ_INDEX   0
#define VIRTIO_GPU_CURSORQ_INDEX 1
#define VIRTIO_GPU_KERNEL_BASE   0xC0000000u

typedef struct virtio_gpu_queue {
    uint16_t size;
    uint16_t pages;
    struct vring_desc *desc;
    struct vring_avail *avail;
    struct vring_used *used;
    void *mem;
    uint16_t last_used_idx;
} virtio_gpu_queue_t;

typedef struct virtio_gpu_dev {
    uint16_t io_base;
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    virtio_gpu_queue_t ctrlq;
    virtio_gpu_queue_t cursorq;
    uint8_t initialized;
} virtio_gpu_dev_t;

static virtio_gpu_dev_t vgpu_dev;

static uint32_t virtio_gpu_phys_addr(const void *ptr) {
    uintptr_t addr = (uintptr_t)ptr;

#ifdef HOST_TEST
    return (uint32_t)addr;
#else
    if (addr >= VIRTIO_GPU_KERNEL_BASE) {
        addr -= VIRTIO_GPU_KERNEL_BASE;
    }
    return (uint32_t)addr;
#endif
}

static void virtio_gpu_fail(uint16_t io_base) {
    if (io_base != 0) {
        outb(io_base + VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_FAILED);
    }
}

static void virtio_gpu_release_queue(virtio_gpu_queue_t *queue) {
    if (queue->mem != NULL && queue->pages != 0) {
        pmm_free_contiguous(queue->mem, queue->pages);
    }
    memset(queue, 0, sizeof(*queue));
}

static int virtio_gpu_setup_queue(virtio_gpu_dev_t *dev, uint16_t queue_index,
                                  virtio_gpu_queue_t *queue) {
    uint32_t avail_end;
    uint32_t used_offset;
    uint32_t total_size;
    uint32_t phys;

    outw(dev->io_base + VIRTIO_REG_QUEUE_SELECT, queue_index);
    queue->size = inw(dev->io_base + VIRTIO_REG_QUEUE_SIZE);
    if (queue->size == 0) {
        return -1;
    }

    avail_end = 16U * queue->size + 6U + 2U * queue->size;
    used_offset = (avail_end + 4095U) & ~4095U;
    total_size = used_offset + 6U + 8U * queue->size;
    queue->pages = (uint16_t)((total_size + 4095U) / 4096U);
    queue->mem = pmm_alloc_contiguous(queue->pages);
    if (!queue->mem) {
        return -1;
    }

    memset(queue->mem, 0, queue->pages * 4096U);
    queue->desc = (struct vring_desc *)queue->mem;
    queue->avail = (struct vring_avail *)((char *)queue->mem + 16U * queue->size);
    queue->used = (struct vring_used *)((char *)queue->mem + used_offset);
    queue->last_used_idx = 0;

    phys = virtio_gpu_phys_addr(queue->mem);
    outl(dev->io_base + VIRTIO_REG_QUEUE_ADDR, phys / 4096U);
    return 0;
}

int virtio_gpu_setup(uint8_t bus, uint8_t slot, uint8_t func) {
    virtio_gpu_release_queue(&vgpu_dev.ctrlq);
    virtio_gpu_release_queue(&vgpu_dev.cursorq);
    memset(&vgpu_dev, 0, sizeof(vgpu_dev));
    vgpu_dev.bus = bus;
    vgpu_dev.slot = slot;
    vgpu_dev.func = func;
    vgpu_dev.io_base = virtio_get_io_base(bus, slot, func);
    if (!vgpu_dev.io_base) {
        return -1;
    }

    outb(vgpu_dev.io_base + VIRTIO_REG_DEVICE_STATUS, 0);
    outb(vgpu_dev.io_base + VIRTIO_REG_DEVICE_STATUS,
         VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    if (virtio_gpu_setup_queue(&vgpu_dev, VIRTIO_GPU_CTRLQ_INDEX, &vgpu_dev.ctrlq) < 0) {
        virtio_gpu_fail(vgpu_dev.io_base);
        return -1;
    }

    if (virtio_gpu_setup_queue(&vgpu_dev, VIRTIO_GPU_CURSORQ_INDEX, &vgpu_dev.cursorq) < 0) {
        virtio_gpu_release_queue(&vgpu_dev.ctrlq);
        virtio_gpu_fail(vgpu_dev.io_base);
        return -1;
    }

    outb(vgpu_dev.io_base + VIRTIO_REG_DEVICE_STATUS,
         VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);
    vgpu_dev.initialized = 1;
    kprint("VirtIO-GPU Initialized.\n");
    return 0;
}
