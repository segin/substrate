/*
 * virtio_scsi.c - VirtIO-SCSI Transport Driver
 *
 * Pass-through driver mapping SCSI requests to VirtIO virtqueues.
 * Implements the VirtIO SCSI Host specification.
 *
 * Queue layout:
 *   0: Control queue (events, task management)
 *   1: Event queue (hot-plug notifications)
 *   2+: Request queues (SCSI commands)
 */

#include <drivers/virtio/virtio.h>
#include <drivers/storage/scsi/scsi.h>
#include <arch/x86-common/io.h>
#include <arch/i386/percpu.h>
#include <arch/i386/pmm.h>
#include <kern/console.h>
#include <string.h>
#include <stdio.h>

/*
 * ============================================================
 * VirtIO-SCSI Device ID and Feature Bits
 * ============================================================
 */

/* Feature bits */
#define VIRTIO_SCSI_F_INOUT       (1 << 0)   /* Single request for IN/OUT */
#define VIRTIO_SCSI_F_HOTPLUG     (1 << 1)   /* Hot-plug support */
#define VIRTIO_SCSI_F_CHANGE      (1 << 2)   /* LUN change notification */
#define VIRTIO_SCSI_F_T10_PI      (1 << 3)   /* T10 protection information */

/* VirtIO-SCSI config registers (after common) */
#define VIRTIO_SCSI_REG_NUM_QUEUES     0x14  /* Number of request queues */
#define VIRTIO_SCSI_REG_SEG_MAX        0x18  /* Max segments per request */
#define VIRTIO_SCSI_REG_MAX_SECTORS    0x1C  /* Max sectors per request */
#define VIRTIO_SCSI_REG_CMD_PER_LUN    0x20  /* Commands per LUN */
#define VIRTIO_SCSI_REG_EVENT_INFO_SIZE 0x24
#define VIRTIO_SCSI_REG_SENSE_SIZE     0x28  /* Max sense data size */
#define VIRTIO_SCSI_REG_CDB_SIZE       0x2C  /* Max CDB size */
#define VIRTIO_SCSI_REG_MAX_CHANNEL    0x30
#define VIRTIO_SCSI_REG_MAX_TARGET     0x32
#define VIRTIO_SCSI_REG_MAX_LUN        0x34

/* Request/Response status */
#define VIRTIO_SCSI_S_OK               0     /* Success */
#define VIRTIO_SCSI_S_OVERRUN          1     /* Data overrun */
#define VIRTIO_SCSI_S_ABORTED          2     /* Request aborted */
#define VIRTIO_SCSI_S_BAD_TARGET       3     /* No such target */
#define VIRTIO_SCSI_S_RESET            4     /* Reset occurred */
#define VIRTIO_SCSI_S_BUSY             5     /* Busy, try again */
#define VIRTIO_SCSI_S_TRANSPORT_FAILURE 6    /* Transport failure */
#define VIRTIO_SCSI_S_TARGET_FAILURE   7     /* Target failure */
#define VIRTIO_SCSI_S_NEXUS_FAILURE    8     /* Nexus failure */
#define VIRTIO_SCSI_S_FAILURE          9     /* Unspecified failure */

/* Task management */
#define VIRTIO_SCSI_T_TMF              0     /* Task management function */
#define VIRTIO_SCSI_T_AN_QUERY         1     /* Async notification query */
#define VIRTIO_SCSI_T_AN_SUBSCRIBE     2     /* Async notification subscribe */

/* TMF Subtypes */
#define VIRTIO_SCSI_T_TMF_ABORT_TASK      0
#define VIRTIO_SCSI_T_TMF_ABORT_TASK_SET  1
#define VIRTIO_SCSI_T_TMF_CLEAR_ACA       2
#define VIRTIO_SCSI_T_TMF_CLEAR_TASK_SET  3
#define VIRTIO_SCSI_T_TMF_I_T_NEXUS_RESET 4
#define VIRTIO_SCSI_T_TMF_LOGICAL_UNIT_RESET 5
#define VIRTIO_SCSI_T_TMF_QUERY_TASK      6
#define VIRTIO_SCSI_T_TMF_QUERY_TASK_SET  7

/* TMF Response Codes */
#define VIRTIO_SCSI_S_FUNCTION_COMPLETE   0
#define VIRTIO_SCSI_S_FUNCTION_SUCCEEDED  10
#define VIRTIO_SCSI_S_FUNCTION_REJECTED   11

/* Event types */
#define VIRTIO_SCSI_T_NO_EVENT         0
#define VIRTIO_SCSI_T_TRANSPORT_RESET  1
#define VIRTIO_SCSI_T_ASYNC_NOTIFY     2
#define VIRTIO_SCSI_T_PARAM_CHANGE     3

/*
 * ============================================================
 * VirtIO-SCSI Request/Response Structures
 * ============================================================
 */

/* Task Management Function (TMF) Request */
struct virtio_scsi_ctrl_tmf_req {
    uint32_t type;
    uint32_t subtype;
    uint8_t  lun[8];
    uint64_t id;
} __attribute__((packed));

/* Task Management Function (TMF) Response */
struct virtio_scsi_ctrl_tmf_resp {
    uint8_t response;
} __attribute__((packed));

/* SCSI request header (device-readable) */
struct virtio_scsi_req_hdr {
    uint8_t  lun[8];            /* Logical Unit Number */
    uint64_t tag;               /* Request tag */
    uint8_t  task_attr;         /* Task attribute */
    uint8_t  prio;              /* Priority */
    uint8_t  crn;               /* Command reference number */
    uint8_t  cdb[32];           /* Command descriptor block */
} __attribute__((packed));

/* SCSI response header (device-writable) */
struct virtio_scsi_resp_hdr {
    uint32_t sense_len;         /* Sense data length */
    uint32_t residual;          /* Residual byte count */
    uint16_t status_qualifier;  /* Status qualifier */
    uint8_t  status;            /* SCSI status byte */
    uint8_t  response;          /* VirtIO response code */
    uint8_t  sense[96];         /* Sense data */
} __attribute__((packed));

/* Event notification */
struct virtio_scsi_event {
    uint32_t event;
    uint8_t  lun[8];
    uint32_t reason;
} __attribute__((packed));

#define VIRTIO_SCSI_KERNEL_BASE 0xC0000000u
#define VIRTIO_SCSI_EVENT_SLOTS 4

/*
 * ============================================================
 * Driver State
 * ============================================================
 */

#define VIRTIO_SCSI_MAX_QUEUES  4
#define VIRTIO_SCSI_QUEUE_SIZE  64

typedef struct virtio_scsi_queue {
    uint16_t size;
    uint16_t pages;
    struct vring_desc *desc;
    struct vring_avail *avail;
    struct vring_used *used;
    void *mem;
    uint16_t last_used_idx;
} virtio_scsi_queue_t;

typedef struct virtio_scsi_dev {
    uint16_t io_base;
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  func;
    uint32_t features;
    
    /* Limits from config */
    uint32_t num_queues;
    uint32_t seg_max;
    uint32_t max_sectors;
    uint32_t cmd_per_lun;
    uint32_t sense_size;
    uint32_t cdb_size;
    uint16_t max_channel;
    uint16_t max_target;
    uint32_t max_lun;
    
    /* Queues */
    virtio_scsi_queue_t ctrl_queue;   /* Queue 0 */
    virtio_scsi_queue_t event_queue;  /* Queue 1 */
    virtio_scsi_queue_t req_queue;    /* Queue 2 (use first of N) */
    virtio_scsi_queue_t extra_req_queues[VIRTIO_SCSI_MAX_QUEUES - 1];
    uint16_t req_queue_count;

    /* DMA staging buffers */
    void *ctrl_buf;
    void *req_buf;
    void *extra_req_bufs[VIRTIO_SCSI_MAX_QUEUES - 1];
    struct virtio_scsi_event *event_bufs;
    
    /* SCSI link */
    scsi_link_t link;
} virtio_scsi_dev_t;

static virtio_scsi_dev_t vscsi_dev;
static int vscsi_initialized = 0;

static uint32_t vscsi_phys_addr(const void *ptr) {
    uintptr_t addr = (uintptr_t)ptr;

#ifdef HOST_TEST
    return (uint32_t)addr;
#else
    if (addr >= VIRTIO_SCSI_KERNEL_BASE) {
        addr -= VIRTIO_SCSI_KERNEL_BASE;
    }
    return (uint32_t)addr;
#endif
}

static virtio_scsi_queue_t *vscsi_get_req_queue(virtio_scsi_dev_t *dev, uint16_t index) {
    if (index == 0) {
        return &dev->req_queue;
    }
    if (index >= dev->req_queue_count) {
        return NULL;
    }
    return &dev->extra_req_queues[index - 1];
}

static void *vscsi_get_req_buf(virtio_scsi_dev_t *dev, uint16_t index) {
    if (index == 0) {
        return dev->req_buf;
    }
    if (index >= dev->req_queue_count) {
        return NULL;
    }
    return dev->extra_req_bufs[index - 1];
}

static uint16_t vscsi_select_req_queue(virtio_scsi_dev_t *dev) {
    int cpu_id;

    if (dev->req_queue_count <= 1) {
        return 0;
    }

    cpu_id = percpu_get_cpu_id();
    if (cpu_id < 0) {
        cpu_id = 0;
    }

    return (uint16_t)(cpu_id % dev->req_queue_count);
}

/*
 * ============================================================
 * Queue Management
 * ============================================================
 */

static int vscsi_setup_queue(virtio_scsi_dev_t *dev, int queue_idx, 
                              virtio_scsi_queue_t *q) {
    uint32_t avail_end;
    uint32_t used_offset;
    uint32_t total_size;

    /* Select queue */
    outw(dev->io_base + VIRTIO_REG_QUEUE_SELECT, queue_idx);
    
    /* Get queue size */
    q->size = inw(dev->io_base + VIRTIO_REG_QUEUE_SIZE);
    if (q->size == 0) {
        return -1;  /* Queue not available */
    }
    
    avail_end = 16 * q->size + 6 + 2 * q->size;
    used_offset = (avail_end + 4095) & ~4095;
    total_size = used_offset + 6 + 8 * q->size;
    q->pages = (uint16_t)((total_size + 4095) / 4096);

    /* Allocate queue memory (page aligned, physically contiguous) */
    q->mem = pmm_alloc_contiguous(q->pages);
    if (!q->mem) {
        return -1;
    }
    memset(q->mem, 0, q->pages * 4096U);
    
    /* Setup ring pointers */
    q->desc = (struct vring_desc *)q->mem;
    q->avail = (struct vring_avail *)((char*)q->mem + 16 * q->size);
    
    q->used = (struct vring_used *)((char*)q->mem + used_offset);
    q->last_used_idx = 0;
    
    /* Write physical address */
    uint32_t phys = vscsi_phys_addr(q->mem);
    outl(dev->io_base + VIRTIO_REG_QUEUE_ADDR, phys / 4096);
    
    return 0;
}

/*
 * ============================================================
 * SCSI Transport Callbacks
 * ============================================================
 */

static int vscsi_execute(scsi_link_t *link, scsi_request_t *req) {
    virtio_scsi_dev_t *dev = (virtio_scsi_dev_t *)link->priv;
    uint16_t queue_index;
    uint16_t notify_queue;
    void *req_buf;

    if (!dev || !req || !req->device) return -1;

    queue_index = vscsi_select_req_queue(dev);
    notify_queue = (uint16_t)(2 + queue_index);
    
    virtio_scsi_queue_t *q = vscsi_get_req_queue(dev, queue_index);
    scsi_device_t *sdev = req->device;
    req_buf = vscsi_get_req_buf(dev, queue_index);
    if (!q || !req_buf) return -1;

    struct virtio_scsi_req_hdr *hdr = (struct virtio_scsi_req_hdr *)req_buf;
    struct virtio_scsi_resp_hdr *resp =
        (struct virtio_scsi_resp_hdr *)((uint8_t *)req_buf + sizeof(*hdr));
    
    /* Build request header */
    memset(hdr, 0, sizeof(*hdr));
    memset(resp, 0, sizeof(*resp));
    
    /* LUN format: first byte is 1, second is target, bytes 2-3 are LUN */
    hdr->lun[0] = 1;
    hdr->lun[1] = sdev->target;
    hdr->lun[2] = (sdev->lun >> 8) & 0xFF;
    hdr->lun[3] = sdev->lun & 0xFF;
    hdr->tag = (uint64_t)(uintptr_t)req;
    hdr->task_attr = 0;  /* Simple queue */
    hdr->prio = 0;
    hdr->crn = 0;
    
    /* Copy CDB */
    uint8_t cdb_len = req->cdb_len;
    if (cdb_len > 32) cdb_len = 32;
    memcpy(hdr->cdb, req->cdb, cdb_len);
    
    /* Build descriptor chain */
    int write_to_device = (req->flags & SCSI_REQ_WRITE) ? 1 : 0;
    int desc_idx = 0;
    
    /* Descriptor 0: Request header (device-readable) */
    q->desc[0].addr = (uint64_t)vscsi_phys_addr(hdr);
    q->desc[0].len = sizeof(*hdr);
    q->desc[0].flags = VRING_DESC_F_NEXT;
    q->desc[0].next = 1;
    desc_idx = 1;
    
    /* Descriptor 1: Data buffer (optional) */
    if (req->data && req->data_len > 0) {
        q->desc[desc_idx].addr = (uint64_t)vscsi_phys_addr(req->data);
        q->desc[desc_idx].len = req->data_len;
        q->desc[desc_idx].flags = VRING_DESC_F_NEXT;
        if (!write_to_device) {
            q->desc[desc_idx].flags |= VRING_DESC_F_WRITE;
        }
        q->desc[desc_idx].next = desc_idx + 1;
        desc_idx++;
    }
    
    /* Final descriptor: Response header (device-writable) */
    q->desc[desc_idx].addr = (uint64_t)vscsi_phys_addr(resp);
    q->desc[desc_idx].len = sizeof(*resp);
    q->desc[desc_idx].flags = VRING_DESC_F_WRITE;
    q->desc[desc_idx].next = 0;
    
    /* Submit to available ring */
    q->avail->ring[q->avail->idx % q->size] = 0;  /* Start of chain */
    __asm__ volatile("mfence" ::: "memory");
    q->avail->idx++;
    
    /* Notify device */
    outw(dev->io_base + VIRTIO_REG_QUEUE_NOTIFY, notify_queue);
    
    /* Poll for completion */
    while (q->last_used_idx == q->used->idx) {
        __asm__ volatile("pause");
    }
    q->last_used_idx++;
    
    /* Parse response */
    if (resp->response != VIRTIO_SCSI_S_OK) {
        req->status = SCSI_STATUS_CHECK_CONDITION;
        req->error = resp->response;
        return -1;
    }
    
    req->status = resp->status;
    req->error = 0;
    req->data_xfer = (resp->residual > req->data_len) ? 0 : (req->data_len - resp->residual);
    
    /* Copy sense data if present */
    if (resp->sense_len > 0) {
        uint32_t copy_len = resp->sense_len;
        if (copy_len > SCSI_MAX_SENSE_LEN) copy_len = SCSI_MAX_SENSE_LEN;
        memcpy(req->sense, resp->sense, copy_len);
        req->sense_len = (uint8_t)copy_len;
    }
    
    return (req->status == SCSI_STATUS_GOOD) ? 0 : -1;
}

static int vscsi_send_tmf(virtio_scsi_dev_t *dev, uint32_t subtype,
                          uint8_t target, uint16_t lun) {
    if (!dev) return -1;
    if (!dev->ctrl_buf) return -1;
    virtio_scsi_queue_t *q = &dev->ctrl_queue;
    struct virtio_scsi_ctrl_tmf_req *req =
        (struct virtio_scsi_ctrl_tmf_req *)dev->ctrl_buf;
    struct virtio_scsi_ctrl_tmf_resp *resp =
        (struct virtio_scsi_ctrl_tmf_resp *)((uint8_t *)dev->ctrl_buf + sizeof(*req));

    /* Build TMF request */
    memset(req, 0, sizeof(*req));
    memset(resp, 0, sizeof(*resp));

    req->type = VIRTIO_SCSI_T_TMF;
    req->subtype = subtype;
    req->lun[0] = 1;
    req->lun[1] = target;
    req->lun[2] = (lun >> 8) & 0xFF;
    req->lun[3] = lun & 0xFF;
    req->id = 0;

    /* Build descriptor chain */
    int desc_idx = 0;

    /* Descriptor 0: Request (device-readable) */
    q->desc[0].addr = (uint64_t)vscsi_phys_addr(req);
    q->desc[0].len = sizeof(*req);
    q->desc[0].flags = VRING_DESC_F_NEXT;
    q->desc[0].next = 1;
    desc_idx = 1;

    /* Descriptor 1: Response (device-writable) */
    q->desc[desc_idx].addr = (uint64_t)vscsi_phys_addr(resp);
    q->desc[desc_idx].len = sizeof(*resp);
    q->desc[desc_idx].flags = VRING_DESC_F_WRITE;
    q->desc[desc_idx].next = 0;

    /* Submit to available ring */
    q->avail->ring[q->avail->idx % q->size] = 0;  /* Start of chain */
    __asm__ volatile("mfence" ::: "memory");
    q->avail->idx++;

    /* Notify device (Queue 0) */
    outw(dev->io_base + VIRTIO_REG_QUEUE_NOTIFY, 0);

    /* Poll for completion */
    while (q->last_used_idx == q->used->idx) {
        __asm__ volatile("pause");
    }
    q->last_used_idx++;

    /* Check response */
    if (resp->response == VIRTIO_SCSI_S_FUNCTION_COMPLETE ||
        resp->response == VIRTIO_SCSI_S_FUNCTION_SUCCEEDED) {
        return 0;
    }

    return resp->response;
}

static int vscsi_reset_device(scsi_link_t *link, scsi_device_t *sdev) {
    virtio_scsi_dev_t *dev = (virtio_scsi_dev_t *)link->priv;
    if (!dev || !sdev) return -1;

    int ret = vscsi_send_tmf(dev, VIRTIO_SCSI_T_TMF_LOGICAL_UNIT_RESET,
                             sdev->target, sdev->lun);

    if (ret == 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "virtio_scsi: reset device %d:%d succeeded\n",
                sdev->target, sdev->lun);
        kprint(buf);
        return 0;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "virtio_scsi: reset device %d:%d failed (resp=%d)\n",
            sdev->target, sdev->lun, ret);
    kprint(buf);
    return -1;
}

static int vscsi_reset_bus(scsi_link_t *link) {
    virtio_scsi_dev_t *dev = (virtio_scsi_dev_t *)link->priv;
    if (!dev) return -1;

    kprint("virtio_scsi: resetting bus (I_T Nexus Reset)\n");

    /* Iterate over all possible targets and reset their I_T nexus */
    for (uint16_t target = 0; target <= dev->max_target; target++) {
        int ret = vscsi_send_tmf(dev, VIRTIO_SCSI_T_TMF_I_T_NEXUS_RESET,
                                 (uint8_t)target, 0);

        if (ret != 0 && ret != VIRTIO_SCSI_S_BAD_TARGET) {
            char buf[64];
            snprintf(buf, sizeof(buf), "virtio_scsi: reset target %d failed (resp=%d)\n",
                    target, ret);
            kprint(buf);
        }
    }

    kprint("virtio_scsi: bus reset complete\n");
    return 0;
}

/*
 * ============================================================
 * Event Queue Handling
 * ============================================================
 */

static void vscsi_process_events(virtio_scsi_dev_t *dev) {
    virtio_scsi_queue_t *eq = &dev->event_queue;
    
    while (eq->last_used_idx != eq->used->idx) {
        uint32_t idx = eq->used->ring[eq->last_used_idx % eq->size].id;
        if (dev->event_bufs == NULL || idx >= VIRTIO_SCSI_EVENT_SLOTS) {
            eq->last_used_idx++;
            continue;
        }
        struct virtio_scsi_event *event = &dev->event_bufs[idx];
        
        switch (event->event) {
        case VIRTIO_SCSI_T_TRANSPORT_RESET:
            kprint("virtio_scsi: transport reset event\n");
            scsi_scan_bus(&dev->link, dev->link.bus_id);
            break;
            
        case VIRTIO_SCSI_T_ASYNC_NOTIFY:
            kprint("virtio_scsi: async notify event\n");
            scsi_scan_bus(&dev->link, dev->link.bus_id);
            break;
            
        case VIRTIO_SCSI_T_PARAM_CHANGE:
            kprint("virtio_scsi: param change event\n");
            scsi_scan_bus(&dev->link, dev->link.bus_id);
            break;
            
        default:
            break;
        }
        
        /* Re-queue the event buffer */
        eq->avail->ring[eq->avail->idx % eq->size] = idx;
        eq->avail->idx++;
        eq->last_used_idx++;
    }
}

static void vscsi_setup_event_buffers(virtio_scsi_dev_t *dev) {
    virtio_scsi_queue_t *eq = &dev->event_queue;

    if (dev->event_bufs == NULL) {
        return;
    }

    uint16_t slots = eq->size;
    if (slots > VIRTIO_SCSI_EVENT_SLOTS) {
        slots = VIRTIO_SCSI_EVENT_SLOTS;
    }

    memset(dev->event_bufs, 0, sizeof(*dev->event_bufs) * slots);

    for (uint16_t i = 0; i < slots; i++) {
        eq->desc[i].addr = (uint64_t)vscsi_phys_addr(&dev->event_bufs[i]);
        eq->desc[i].len = sizeof(struct virtio_scsi_event);
        eq->desc[i].flags = VRING_DESC_F_WRITE;
        eq->desc[i].next = 0;
        
        eq->avail->ring[i] = i;
    }
    eq->avail->idx = slots;
    
    /* Notify device */
    outw(dev->io_base + VIRTIO_REG_QUEUE_NOTIFY, 1);  /* Event queue */
}

/*
 * ============================================================
 * Device Initialization
 * ============================================================
 */

void virtio_scsi_setup(uint8_t bus, uint8_t slot, uint8_t func) {
    virtio_scsi_dev_t *dev = &vscsi_dev;
    
    dev->bus = bus;
    dev->slot = slot;
    dev->func = func;
    dev->io_base = virtio_get_io_base(bus, slot, func);
    
    if (!dev->io_base) {
        kprint("virtio_scsi: no I/O base found\n");
        return;
    }
    
    /* 1. Reset device */
    outb(dev->io_base + VIRTIO_REG_DEVICE_STATUS, 0);
    
    /* 2. Acknowledge and negotiate features */
    outb(dev->io_base + VIRTIO_REG_DEVICE_STATUS, 
         VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
    
    /* Read device features */
    dev->features = inl(dev->io_base + VIRTIO_REG_HOST_FEATURES);
    
    /* Accept hotplug feature if available */
    uint32_t guest_features = 0;
    if (dev->features & VIRTIO_SCSI_F_HOTPLUG) {
        guest_features |= VIRTIO_SCSI_F_HOTPLUG;
    }
    outl(dev->io_base + VIRTIO_REG_GUEST_FEATURES, guest_features);
    
    /* Read config */
    dev->num_queues = inl(dev->io_base + VIRTIO_SCSI_REG_NUM_QUEUES);
    dev->seg_max = inl(dev->io_base + VIRTIO_SCSI_REG_SEG_MAX);
    dev->max_sectors = inl(dev->io_base + VIRTIO_SCSI_REG_MAX_SECTORS);
    dev->cmd_per_lun = inl(dev->io_base + VIRTIO_SCSI_REG_CMD_PER_LUN);
    dev->sense_size = inl(dev->io_base + VIRTIO_SCSI_REG_SENSE_SIZE);
    dev->cdb_size = inl(dev->io_base + VIRTIO_SCSI_REG_CDB_SIZE);
    dev->max_channel = inw(dev->io_base + VIRTIO_SCSI_REG_MAX_CHANNEL);
    dev->max_target = inw(dev->io_base + VIRTIO_SCSI_REG_MAX_TARGET);
    dev->max_lun = inl(dev->io_base + VIRTIO_SCSI_REG_MAX_LUN);
    
    /* Setup queues */
    if (vscsi_setup_queue(dev, 0, &dev->ctrl_queue) < 0) {
        kprint("virtio_scsi: failed to setup control queue\n");
        return;
    }
    
    if (vscsi_setup_queue(dev, 1, &dev->event_queue) < 0) {
        kprint("virtio_scsi: failed to setup event queue\n");
        return;
    }
    
    if (vscsi_setup_queue(dev, 2, &dev->req_queue) < 0) {
        kprint("virtio_scsi: failed to setup request queue\n");
        return;
    }

    dev->req_queue_count = dev->num_queues;
    if (dev->req_queue_count == 0) {
        dev->req_queue_count = 1;
    }
    if (dev->req_queue_count > VIRTIO_SCSI_MAX_QUEUES) {
        dev->req_queue_count = VIRTIO_SCSI_MAX_QUEUES;
    }

    for (uint16_t i = 1; i < dev->req_queue_count; i++) {
        if (vscsi_setup_queue(dev, (int)(2 + i), &dev->extra_req_queues[i - 1]) < 0) {
            kprint("virtio_scsi: failed to setup additional request queue\n");
            return;
        }
    }

    dev->ctrl_buf = pmm_alloc_block();
    dev->req_buf = pmm_alloc_block();
    if (!dev->ctrl_buf || !dev->req_buf) {
        kprint("virtio_scsi: failed to allocate DMA staging buffers\n");
        return;
    }
    memset(dev->ctrl_buf, 0, 4096);
    memset(dev->req_buf, 0, 4096);

    for (uint16_t i = 1; i < dev->req_queue_count; i++) {
        dev->extra_req_bufs[i - 1] = pmm_alloc_block();
        if (!dev->extra_req_bufs[i - 1]) {
            kprint("virtio_scsi: failed to allocate per-queue DMA staging buffer\n");
            return;
        }
        memset(dev->extra_req_bufs[i - 1], 0, 4096);
    }

    if (dev->features & VIRTIO_SCSI_F_HOTPLUG) {
        dev->event_bufs = (struct virtio_scsi_event *)pmm_alloc_block();
        if (!dev->event_bufs) {
            kprint("virtio_scsi: failed to allocate event buffers\n");
            return;
        }
        memset(dev->event_bufs, 0, 4096);
    }
    
    /* 3. Driver OK */
    outb(dev->io_base + VIRTIO_REG_DEVICE_STATUS,
         VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);
    
    /* Setup event queue buffers for hot-plug */
    if (dev->features & VIRTIO_SCSI_F_HOTPLUG) {
        vscsi_setup_event_buffers(dev);
    }
    
    /* Setup SCSI transport link */
    snprintf(dev->link.name, sizeof(dev->link.name), "virtio-scsi0");
    dev->link.bus_id = 0;
    dev->link.max_targets = (dev->max_target + 1 > SCSI_MAX_TARGETS) ? SCSI_MAX_TARGETS : (uint8_t)(dev->max_target + 1);
    dev->link.max_luns = (dev->max_lun + 1 > SCSI_MAX_LUNS) ? SCSI_MAX_LUNS : (uint16_t)(dev->max_lun + 1);
    dev->link.adapter_queue_depth = dev->cmd_per_lun ? (uint16_t)dev->cmd_per_lun : 1;
    dev->link.flags = SCSI_LINK_DMA;
    dev->link.execute = vscsi_execute;
    dev->link.reset_device = vscsi_reset_device;
    dev->link.reset_bus = vscsi_reset_bus;
    dev->link.priv = dev;
    
    /* Register with SCSI mid-layer */
    if (scsi_register_link(&dev->link) < 0) {
        kprint("virtio_scsi: failed to register with SCSI mid-layer\n");
        return;
    }
    
    char log_buf[96];
    snprintf(log_buf, sizeof(log_buf), "virtio_scsi: initialized (channels=%d targets=%d)\n",
            dev->max_channel + 1, dev->max_target + 1);
    kprint(log_buf);
    
    /* Scan for devices */
    scsi_scan_bus(&dev->link, 0);
    
    vscsi_initialized = 1;
}

/*
 * Poll for events (called periodically)
 */
void virtio_scsi_poll(void) {
    if (!vscsi_initialized) return;
    vscsi_process_events(&vscsi_dev);
}

/*
 * Get link for direct access
 */
scsi_link_t *virtio_scsi_get_link(void) {
    return vscsi_initialized ? &vscsi_dev.link : NULL;
}
