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
#include <arch/x86-common/include/io.h>
#include <arch/i386/pmm.h>
#include <kern/console.h>
#include <string.h>
#include <stdio.h>

/*
 * ============================================================
 * VirtIO-SCSI Device ID and Feature Bits
 * ============================================================
 */

#define VIRTIO_SCSI_DEVICE_ID     0x1008

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

/*
 * ============================================================
 * Driver State
 * ============================================================
 */

#define VIRTIO_SCSI_MAX_QUEUES  4
#define VIRTIO_SCSI_QUEUE_SIZE  64

typedef struct virtio_scsi_queue {
    uint16_t size;
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
    
    /* SCSI link */
    scsi_link_t link;
} virtio_scsi_dev_t;

static virtio_scsi_dev_t vscsi_dev;
static int vscsi_initialized = 0;

/*
 * ============================================================
 * Queue Management
 * ============================================================
 */

static int vscsi_setup_queue(virtio_scsi_dev_t *dev, int queue_idx, 
                              virtio_scsi_queue_t *q) {
    /* Select queue */
    outw(dev->io_base + VIRTIO_REG_QUEUE_SELECT, queue_idx);
    
    /* Get queue size */
    q->size = inw(dev->io_base + VIRTIO_REG_QUEUE_SIZE);
    if (q->size == 0) {
        return -1;  /* Queue not available */
    }
    
    /* Allocate queue memory (page aligned) */
    q->mem = pmm_alloc_block();
    if (!q->mem) {
        return -1;
    }
    memset(q->mem, 0, 4096);
    
    /* Setup ring pointers */
    q->desc = (struct vring_desc *)q->mem;
    q->avail = (struct vring_avail *)((char*)q->mem + 16 * q->size);
    
    uint32_t avail_end = 16 * q->size + 6 + 2 * q->size;
    uint32_t used_offset = (avail_end + 4095) & ~4095;
    
    if (used_offset + 6 + 8 * q->size > 4096) {
        /* Queue too large for single page */
        pmm_free_block(q->mem);
        return -1;
    }
    
    q->used = (struct vring_used *)((char*)q->mem + used_offset);
    q->last_used_idx = 0;
    
    /* Write physical address */
    uint32_t phys = (uint32_t)(uintptr_t)q->mem - 0xC0000000;
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
    if (!dev || !req || !req->device) return -1;
    
    virtio_scsi_queue_t *q = &dev->req_queue;
    scsi_device_t *sdev = req->device;
    
    /* Build request header */
    struct virtio_scsi_req_hdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    
    /* LUN format: first byte is 1, second is target, bytes 2-3 are LUN */
    hdr.lun[0] = 1;
    hdr.lun[1] = sdev->target;
    hdr.lun[2] = (sdev->lun >> 8) & 0xFF;
    hdr.lun[3] = sdev->lun & 0xFF;
    hdr.tag = (uint64_t)(uintptr_t)req;
    hdr.task_attr = 0;  /* Simple queue */
    hdr.prio = 0;
    hdr.crn = 0;
    
    /* Copy CDB */
    uint8_t cdb_len = req->cdb_len;
    if (cdb_len > 32) cdb_len = 32;
    memcpy(hdr.cdb, req->cdb, cdb_len);
    
    /* Response buffer */
    struct virtio_scsi_resp_hdr resp;
    memset(&resp, 0, sizeof(resp));
    
    /* Build descriptor chain */
    int write_to_device = (req->flags & SCSI_REQ_WRITE) ? 1 : 0;
    int desc_idx = 0;
    
    /* Descriptor 0: Request header (device-readable) */
    q->desc[0].addr = (uint64_t)(uint32_t)&hdr;
    q->desc[0].len = sizeof(hdr);
    q->desc[0].flags = VRING_DESC_F_NEXT;
    q->desc[0].next = 1;
    desc_idx = 1;
    
    /* Descriptor 1: Data buffer (optional) */
    if (req->data && req->data_len > 0) {
        q->desc[desc_idx].addr = (uint64_t)(uint32_t)req->data;
        q->desc[desc_idx].len = req->data_len;
        q->desc[desc_idx].flags = VRING_DESC_F_NEXT;
        if (!write_to_device) {
            q->desc[desc_idx].flags |= VRING_DESC_F_WRITE;
        }
        q->desc[desc_idx].next = desc_idx + 1;
        desc_idx++;
    }
    
    /* Final descriptor: Response header (device-writable) */
    q->desc[desc_idx].addr = (uint64_t)(uint32_t)&resp;
    q->desc[desc_idx].len = sizeof(resp);
    q->desc[desc_idx].flags = VRING_DESC_F_WRITE;
    q->desc[desc_idx].next = 0;
    
    /* Submit to available ring */
    q->avail->ring[q->avail->idx % q->size] = 0;  /* Start of chain */
    __asm__ volatile("mfence" ::: "memory");
    q->avail->idx++;
    
    /* Notify device */
    outw(dev->io_base + VIRTIO_REG_QUEUE_NOTIFY, 2);  /* Request queue */
    
    /* Poll for completion */
    while (q->last_used_idx == q->used->idx) {
        __asm__ volatile("pause");
    }
    q->last_used_idx++;
    
    /* Parse response */
    if (resp.response != VIRTIO_SCSI_S_OK) {
        req->status = SCSI_STATUS_CHECK_CONDITION;
        req->error = resp.response;
        return -1;
    }
    
    req->status = resp.status;
    req->data_xfer = req->data_len - resp.residual;
    
    /* Copy sense data if present */
    if (resp.sense_len > 0) {
        uint32_t copy_len = resp.sense_len;
        if (copy_len > SCSI_MAX_SENSE_LEN) copy_len = SCSI_MAX_SENSE_LEN;
        memcpy(req->sense, resp.sense, copy_len);
        req->sense_len = (uint8_t)copy_len;
    }
    
    return (req->status == SCSI_STATUS_GOOD) ? 0 : -1;
}

static int vscsi_reset_device(scsi_link_t *link, scsi_device_t *sdev) {
    virtio_scsi_dev_t *dev = (virtio_scsi_dev_t *)link->priv;
    if (!dev || !sdev) return -1;

    virtio_scsi_queue_t *q = &dev->ctrl_queue;

    /* Build TMF request */
    struct virtio_scsi_ctrl_tmf_req req;
    memset(&req, 0, sizeof(req));

    req.type = VIRTIO_SCSI_T_TMF;
    req.subtype = VIRTIO_SCSI_T_TMF_LOGICAL_UNIT_RESET;
    req.lun[0] = 1;
    req.lun[1] = sdev->target;
    req.lun[2] = (sdev->lun >> 8) & 0xFF;
    req.lun[3] = sdev->lun & 0xFF;
    req.id = 0;

    /* Build TMF response */
    struct virtio_scsi_ctrl_tmf_resp resp;
    memset(&resp, 0, sizeof(resp));

    /* Build descriptor chain */
    int desc_idx = 0;

    /* Descriptor 0: Request (device-readable) */
    q->desc[0].addr = (uint64_t)(uint32_t)&req;
    q->desc[0].len = sizeof(req);
    q->desc[0].flags = VRING_DESC_F_NEXT;
    q->desc[0].next = 1;
    desc_idx = 1;

    /* Descriptor 1: Response (device-writable) */
    q->desc[desc_idx].addr = (uint64_t)(uint32_t)&resp;
    q->desc[desc_idx].len = sizeof(resp);
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
    if (resp.response == VIRTIO_SCSI_S_FUNCTION_COMPLETE ||
        resp.response == VIRTIO_SCSI_S_FUNCTION_SUCCEEDED) {
        char buf[64];
        sprintf(buf, "virtio_scsi: reset device %d:%d succeeded\n",
                sdev->target, sdev->lun);
        kprint(buf);
        return 0;
    }

    char buf[64];
    sprintf(buf, "virtio_scsi: reset device %d:%d failed (resp=%d)\n",
            sdev->target, sdev->lun, resp.response);
    kprint(buf);
    return -1;
}

static int vscsi_reset_bus(scsi_link_t *link) {
    (void)link;
    /* TODO: Send device reset */
    kprint("virtio_scsi: reset_bus not implemented\n");
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
        struct virtio_scsi_event *event = (struct virtio_scsi_event *)
            (uintptr_t)eq->desc[idx].addr;
        
        switch (event->event) {
        case VIRTIO_SCSI_T_TRANSPORT_RESET:
            kprint("virtio_scsi: transport reset event\n");
            /* Rescan for device changes */
            scsi_scan_bus(&dev->link, 0);
            break;
            
        case VIRTIO_SCSI_T_ASYNC_NOTIFY:
            kprint("virtio_scsi: async notify event\n");
            break;
            
        case VIRTIO_SCSI_T_PARAM_CHANGE:
            kprint("virtio_scsi: param change event\n");
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
    
    /* Allocate event structures and queue them */
    static struct virtio_scsi_event events[4];
    
    for (int i = 0; i < 4 && (uint16_t)i < eq->size; i++) {
        eq->desc[i].addr = (uint64_t)(uint32_t)&events[i];
        eq->desc[i].len = sizeof(struct virtio_scsi_event);
        eq->desc[i].flags = VRING_DESC_F_WRITE;
        eq->desc[i].next = 0;
        
        eq->avail->ring[i] = i;
    }
    eq->avail->idx = 4;
    
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
    
    /* 3. Driver OK */
    outb(dev->io_base + VIRTIO_REG_DEVICE_STATUS,
         VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);
    
    /* Setup event queue buffers for hot-plug */
    if (dev->features & VIRTIO_SCSI_F_HOTPLUG) {
        vscsi_setup_event_buffers(dev);
    }
    
    /* Setup SCSI transport link */
    dev->link.name = "virtio-scsi";
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
