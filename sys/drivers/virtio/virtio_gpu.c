#include <drivers/virtio/virtio.h>

#include <arch/i386/pmm.h>
#include <arch/x86-common/io.h>
#include <kern/console.h>
#include <string.h>

#define VIRTIO_GPU_CTRLQ_INDEX   0
#define VIRTIO_GPU_CURSORQ_INDEX 1
#define VIRTIO_GPU_KERNEL_BASE   0xC0000000u

#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO       0x0100U
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D     0x0101U
#define VIRTIO_GPU_CMD_SET_SCANOUT            0x0103U
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D    0x0105U
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106U
#define VIRTIO_GPU_RESP_OK_NODATA             0x1100U
#define VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM      2U

struct virtio_gpu_ctrl_hdr {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

struct virtio_gpu_resource_create_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

struct virtio_gpu_mem_entry {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_resource_attach_backing {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
} __attribute__((packed));

struct virtio_gpu_set_scanout {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect rect;
    uint32_t scanout_id;
    uint32_t resource_id;
} __attribute__((packed));

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
    uint32_t scanout_resource_id;
} virtio_gpu_dev_t;

static virtio_gpu_dev_t vgpu_dev;

static uintptr_t virtio_gpu_phys_addr(const void *ptr) {
    uintptr_t addr = (uintptr_t)ptr;

#ifdef HOST_TEST
    return addr;
#else
    if (addr >= VIRTIO_GPU_KERNEL_BASE) {
        addr -= VIRTIO_GPU_KERNEL_BASE;
    }
    return addr;
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

    phys = (uint32_t)virtio_gpu_phys_addr(queue->mem);
    outl(dev->io_base + VIRTIO_REG_QUEUE_ADDR, phys / 4096U);
    return 0;
}

static int virtio_gpu_submit_ctrl(virtio_gpu_dev_t *dev,
                                  void *req,
                                  uint32_t req_len,
                                  void *data,
                                  uint32_t data_len,
                                  int data_write,
                                  struct virtio_gpu_ctrl_hdr *resp,
                                  uint32_t resp_len) {
    virtio_gpu_queue_t *queue;
    uint16_t head;
    uint16_t resp_idx;
    uint16_t avail_slot;

    if (!dev->initialized) {
        return -1;
    }

    queue = &dev->ctrlq;
    head = 0;
    resp_idx = 1;

    queue->desc[head].addr = (uint64_t)virtio_gpu_phys_addr(req);
    queue->desc[head].len = req_len;
    queue->desc[head].flags = VRING_DESC_F_NEXT;
    queue->desc[head].next = resp_idx;

    if (data != NULL && data_len != 0) {
        uint16_t data_idx = 1;
        resp_idx = 2;

        queue->desc[head].next = data_idx;
        queue->desc[data_idx].addr = (uint64_t)virtio_gpu_phys_addr(data);
        queue->desc[data_idx].len = data_len;
        queue->desc[data_idx].flags = VRING_DESC_F_NEXT | (data_write ? VRING_DESC_F_WRITE : 0);
        queue->desc[data_idx].next = resp_idx;
    }

    queue->desc[resp_idx].addr = (uint64_t)virtio_gpu_phys_addr(resp);
    queue->desc[resp_idx].len = resp_len;
    queue->desc[resp_idx].flags = VRING_DESC_F_WRITE;
    queue->desc[resp_idx].next = 0;

    avail_slot = (uint16_t)(queue->avail->idx % queue->size);
    queue->avail->ring[avail_slot] = head;
    __asm__ volatile("" ::: "memory");
    queue->avail->idx++;

    outw(dev->io_base + VIRTIO_REG_QUEUE_NOTIFY, VIRTIO_GPU_CTRLQ_INDEX);

    while (queue->last_used_idx == queue->used->idx) {
        __asm__ volatile("pause");
    }
    queue->last_used_idx++;

    if (resp->type != VIRTIO_GPU_RESP_OK_NODATA) {
        return -1;
    }

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

int virtio_gpu_create_scanout_resource(uint32_t resource_id,
                                       uint32_t width,
                                       uint32_t height,
                                       void *backing,
                                       size_t backing_len) {
    struct virtio_gpu_resource_create_2d create_req;
    struct virtio_gpu_resource_attach_backing attach_req;
    struct virtio_gpu_mem_entry entry;
    struct virtio_gpu_set_scanout scanout_req;
    struct virtio_gpu_ctrl_hdr resp;

    if (!vgpu_dev.initialized || backing == NULL || backing_len == 0 || width == 0 || height == 0) {
        return -1;
    }

    memset(&create_req, 0, sizeof(create_req));
    create_req.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    create_req.resource_id = resource_id;
    create_req.format = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;
    create_req.width = width;
    create_req.height = height;
    memset(&resp, 0, sizeof(resp));
    if (virtio_gpu_submit_ctrl(&vgpu_dev, &create_req, sizeof(create_req), NULL, 0, 0,
                               &resp, sizeof(resp)) < 0) {
        return -1;
    }

    memset(&attach_req, 0, sizeof(attach_req));
    attach_req.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    attach_req.resource_id = resource_id;
    attach_req.nr_entries = 1;
    memset(&entry, 0, sizeof(entry));
    entry.addr = virtio_gpu_phys_addr(backing);
    entry.length = (uint32_t)backing_len;
    memset(&resp, 0, sizeof(resp));
    if (virtio_gpu_submit_ctrl(&vgpu_dev, &attach_req, sizeof(attach_req), &entry, sizeof(entry), 0,
                               &resp, sizeof(resp)) < 0) {
        return -1;
    }

    memset(&scanout_req, 0, sizeof(scanout_req));
    scanout_req.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    scanout_req.rect.width = width;
    scanout_req.rect.height = height;
    scanout_req.scanout_id = 0;
    scanout_req.resource_id = resource_id;
    memset(&resp, 0, sizeof(resp));
    if (virtio_gpu_submit_ctrl(&vgpu_dev, &scanout_req, sizeof(scanout_req), NULL, 0, 0,
                               &resp, sizeof(resp)) < 0) {
        return -1;
    }

    vgpu_dev.scanout_resource_id = resource_id;
    return 0;
}
