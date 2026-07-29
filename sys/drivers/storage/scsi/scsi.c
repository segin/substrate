/*
 * scsi.c - SCSI Mid-Layer (CAM-like) Implementation
 *
 * Core SCSI abstraction providing:
 * - Device registry
 * - Request allocation and queueing
 * - Command execution with timeout/retry
 * - Sense data parsing
 * - CDB construction helpers
 */

#include <stdio.h>
#include <string.h>

#include <drivers/storage/scsi/scsi.h>
#include <kern/console.h>
#include <kern/time.h>
#include <sys/lock.h>

/* Serialises the request free-list and the device lists below.  Two threads
 * driving I/O on different disks (e.g. AHCI + USB) enter scsi_request_alloc()
 * concurrently on this SMP/preemptible stack; without this they pop the same
 * scsi_request_t and cross-wire each other's CDBs/status.  IRQ-safe so it is
 * correct even if a completion path ever frees a request from interrupt
 * context. */
static spinlock_t scsi_pool_lock = SPINLOCK_INIT("scsi_pool");

/* Kernel time function */
static inline uint64_t kernel_time_ms(void) {
    return (uint64_t)get_uptime_ms();
}

static void scsi_delay_ms(uint32_t delay_ms) {
    uint64_t start = kernel_time_ms();

    while ((kernel_time_ms() - start) < delay_ms) {
        __asm__ volatile("pause");
    }
}

struct scsi_read_capacity_16 {
    uint8_t last_lba[8];
    uint8_t block_size[4];
    uint8_t prot_p_type;
    uint8_t reserved[19];
} __attribute__((packed));

static void scsi_cdb_read_capacity_16(uint8_t *cdb, uint32_t alloc_len) {
    memset(cdb, 0, 16);
    cdb[0] = SCSI_CMD_READ_CAPACITY_16;
    cdb[1] = 0x10; /* service action */
    scsi_put_be32(&cdb[10], alloc_len);
}

/*
 * ============================================================
 * Static Storage
 * ============================================================
 */

/* Device Registry */
static scsi_device_t *scsi_device_list = NULL;
static int scsi_device_count = 0;
static uint32_t scsi_next_device_num = 0;

/* Transport Registry */
#define SCSI_MAX_LINKS 8
static scsi_link_t *scsi_links[SCSI_MAX_LINKS];
static int scsi_link_count = 0;

/* Request Pool */
/* Bumped from 32 → 256.  Under heavy concurrent I/O (multiple block
 * devices each issuing reads from filesystem callers) the smaller pool
 * exhausted silently and callers that didn't check for NULL produced
 * mysterious EIOs.  256 is still tiny against the kernel heap. */
#define SCSI_REQUEST_POOL_SIZE 256
static scsi_request_t scsi_request_pool[SCSI_REQUEST_POOL_SIZE];
static scsi_request_t *scsi_free_requests = NULL;

/* Device Pool */
#define SCSI_DEVICE_POOL_SIZE 16
static scsi_device_t scsi_device_pool[SCSI_DEVICE_POOL_SIZE];
static scsi_device_t *scsi_free_devices = NULL;

/*
 * ============================================================
 * Initialization
 * ============================================================
 */

void scsi_init(void) {
    kprint("SCSI: Initializing mid-layer...\n");

    scsi_device_list = NULL;
    scsi_device_count = 0;
    
    /* Initialize request pool */
    scsi_free_requests = NULL;
    for (int i = SCSI_REQUEST_POOL_SIZE - 1; i >= 0; i--) {
        memset(&scsi_request_pool[i], 0, sizeof(scsi_request_t));
        scsi_request_pool[i].state = SCSI_REQ_STATE_FREE;
        scsi_request_pool[i].next = scsi_free_requests;
        scsi_free_requests = &scsi_request_pool[i];
    }
    
    /* Initialize device pool */
    scsi_free_devices = NULL;
    for (int i = SCSI_DEVICE_POOL_SIZE - 1; i >= 0; i--) {
        memset(&scsi_device_pool[i], 0, sizeof(scsi_device_t));
        scsi_device_pool[i].next = scsi_free_devices;
        scsi_free_devices = &scsi_device_pool[i];
    }
    
    /* Clear transport registry */
    for (int i = 0; i < SCSI_MAX_LINKS; i++) {
        scsi_links[i] = NULL;
    }
    scsi_link_count = 0;
    scsi_next_device_num = 0;

    scsi_dev_init();
    scsi_ctl_init();
    
    kprint("SCSI: Mid-layer initialized (");
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", SCSI_REQUEST_POOL_SIZE);
    kprint(buf);
    kprint(" request slots, ");
    snprintf(buf, sizeof(buf), "%d", SCSI_DEVICE_POOL_SIZE);
    kprint(buf);
    kprint(" device slots)\n");
}

/*
 * ============================================================
 * Transport Registration
 * ============================================================
 */

int scsi_register_link(scsi_link_t *link) {
    if (!link || !link->execute) {
        return -1;
    }
    
    if (scsi_link_count >= SCSI_MAX_LINKS) {
        kprint("SCSI: Too many transport links\n");
        return -1;
    }

    if (link->max_targets == 0) {
        link->max_targets = SCSI_MAX_TARGETS;
    }
    if (link->max_luns == 0) {
        link->max_luns = 8;
    }
    if (link->adapter_queue_depth == 0) {
        link->adapter_queue_depth = 1;
    }
    
    scsi_links[scsi_link_count++] = link;
    scsi_create_bus_node(link, link->bus_id);
    scsi_scan_bus(link, link->bus_id);
    
    kprint("SCSI: Registered transport '");
    kprint(link->name[0] ? link->name : "unknown");
    kprint("'\n");
    
    return 0;
}

void scsi_unregister_link(scsi_link_t *link) {
    scsi_device_t *dev = scsi_device_list;
    while (dev) {
        scsi_device_t *next = dev->next;
        if (dev->link == link) {
            scsi_dev_detach(dev);
            scsi_device_free(dev);
        }
        dev = next;
    }

    for (int i = 0; i < scsi_link_count; i++) {
        if (scsi_links[i] == link) {
            /* Shift remaining entries */
            for (int j = i; j < scsi_link_count - 1; j++) {
                scsi_links[j] = scsi_links[j + 1];
            }
            scsi_links[--scsi_link_count] = NULL;
            return;
        }
    }
}

/*
 * ============================================================
 * Device Management
 * ============================================================
 */

scsi_device_t *scsi_device_alloc(void) {
    unsigned long flags = spinlock_acquire_irq(&scsi_pool_lock);
    if (!scsi_free_devices) {
        spinlock_release_irq(&scsi_pool_lock, flags);
        kprint("SCSI: Device pool exhausted\n");
        return NULL;
    }

    scsi_device_t *dev = scsi_free_devices;
    scsi_free_devices = dev->next;
    spinlock_release_irq(&scsi_pool_lock, flags);

    memset(dev, 0, sizeof(scsi_device_t));
    dev->max_queue_depth = 1;  /* Default single-command queue */
    
    return dev;
}

void scsi_device_free(scsi_device_t *dev) {
    if (!dev) return;
    
    /* Ensure device is not in the registry (takes scsi_pool_lock itself, so
     * this must run BEFORE we grab the lock for the free-list push below). */
    scsi_device_unregister(dev);

    /* Return to pool */
    memset(dev, 0, sizeof(scsi_device_t));
    unsigned long flags = spinlock_acquire_irq(&scsi_pool_lock);
    dev->next = scsi_free_devices;
    scsi_free_devices = dev;
    spinlock_release_irq(&scsi_pool_lock, flags);
}

int scsi_device_register(scsi_device_t *dev) {
    if (!dev) return -1;
    
    /* Duplicate check + insert must be atomic against a concurrent register
     * of the same address.  scsi_device_lookup() is a lock-free read walk, so
     * calling it under the pool lock does not nest. */
    unsigned long flags = spinlock_acquire_irq(&scsi_pool_lock);
    if (scsi_device_lookup(dev->bus, dev->target, dev->lun)) {
        spinlock_release_irq(&scsi_pool_lock, flags);
        kprint("SCSI: Device already registered at ");
        char buf[32];
        snprintf(buf, sizeof(buf), "%d:%d:%d\n", dev->bus, dev->target, dev->lun);
        kprint(buf);
        return -1;
    }

    /* Add to list */
    dev->next = scsi_device_list;
    scsi_device_list = dev;
    scsi_device_count++;
    dev->device_num = scsi_next_device_num++;
    spinlock_release_irq(&scsi_pool_lock, flags);

    /* Log registration */
    char buf[128];
    snprintf(buf, sizeof(buf), "scsi: %d:%d:%d %s %s [0x%02x]\n",
             dev->bus, dev->target, dev->lun,
             dev->vendor, dev->product, dev->type);
    kprint(buf);
    
    return 0;
}

void scsi_device_unregister(scsi_device_t *dev) {
    if (!dev) return;
    
    unsigned long flags = spinlock_acquire_irq(&scsi_pool_lock);
    scsi_device_t **pp = &scsi_device_list;
    while (*pp) {
        if (*pp == dev) {
            *pp = dev->next;
            dev->next = NULL;
            scsi_device_count--;
            spinlock_release_irq(&scsi_pool_lock, flags);
            return;
        }
        pp = &(*pp)->next;
    }
    spinlock_release_irq(&scsi_pool_lock, flags);
}

scsi_device_t *scsi_device_lookup(uint8_t bus, uint8_t target, uint16_t lun) {
    for (scsi_device_t *dev = scsi_device_list; dev; dev = dev->next) {
        if (dev->bus == bus && dev->target == target && dev->lun == lun) {
            return dev;
        }
    }
    return NULL;
}

/*
 * ============================================================
 * Request Management
 * ============================================================
 */

scsi_request_t *scsi_request_alloc(void) {
    unsigned long flags = spinlock_acquire_irq(&scsi_pool_lock);
    if (!scsi_free_requests) {
        spinlock_release_irq(&scsi_pool_lock, flags);
        kprint("SCSI: Request pool exhausted\n");
        return NULL;
    }

    scsi_request_t *req = scsi_free_requests;
    scsi_free_requests = req->next;
    spinlock_release_irq(&scsi_pool_lock, flags);

    memset(req, 0, sizeof(scsi_request_t));
    req->state = SCSI_REQ_STATE_PENDING;
    req->timeout_ms = 30000;  /* 30 second default */
    req->retries = 0;
    req->max_retries = 3;
    req->submit_time = kernel_time_ms();
    
    return req;
}

void scsi_request_free(scsi_request_t *req) {
    if (!req) return;

    memset(req, 0, sizeof(scsi_request_t));
    req->state = SCSI_REQ_STATE_FREE;
    unsigned long flags = spinlock_acquire_irq(&scsi_pool_lock);
    req->next = scsi_free_requests;
    scsi_free_requests = req;
    spinlock_release_irq(&scsi_pool_lock, flags);
}

void scsi_request_init(scsi_request_t *req, scsi_device_t *dev) {
    if (!req) return;
    
    memset(req->cdb, 0, SCSI_MAX_CDB_LEN);
    req->device = dev;
    req->data = NULL;
    req->data_len = 0;
    req->data_xfer = 0;
    req->flags = 0;
    req->sense_len = 0;
    req->status = 0;
    req->state = SCSI_REQ_STATE_PENDING;
    req->error = 0;
    req->timeout_ms = 30000;
    req->retries = 0;
    req->max_retries = 3;
    req->callback = NULL;
    req->callback_arg = NULL;
    req->submit_time = kernel_time_ms();
}

/*
 * ============================================================
 * Command Execution
 * ============================================================
 */

int scsi_execute(scsi_request_t *req) {
    int sense_key;
    int ret = -1;
    int should_retry = 0;
    uint32_t retry_delay_ms = 0;
    uint32_t attempts;
    if (!req || !req->device || !req->device->link) {
        return -1;
    }
    
    scsi_link_t *link = req->device->link;
    attempts = req->max_retries + 1;
    
    while (attempts > 0) {
        req->state = SCSI_REQ_STATE_ACTIVE;
        req->sense_len = 0;
        req->error = 0;
        link->commands_issued++;
        
        /* Record start time for timeout tracking */
        req->start_time = kernel_time_ms();
        
        /* Execute via transport */
        ret = link->execute(link, req);
        
        /* Calculate elapsed time */
        req->elapsed_ms = kernel_time_ms() - req->start_time;
        
        if (ret >= 0 && req->status != SCSI_STATUS_CHECK_CONDITION &&
            req->status != SCSI_STATUS_BUSY &&
            req->status != SCSI_STATUS_TASK_SET_FULL) {
            req->state = SCSI_REQ_STATE_COMPLETE;
            
            /* Update statistics */
            if (req->flags & SCSI_REQ_READ) {
                link->bytes_read += req->data_xfer;
            } else if (req->flags & SCSI_REQ_WRITE) {
                link->bytes_written += req->data_xfer;
            }
            break;  /* Success */
        }

        should_retry = 0;
        retry_delay_ms = 0;

        if (req->status == SCSI_STATUS_CHECK_CONDITION &&
            !(req->flags & SCSI_REQ_NO_SENSE) &&
            req->sense_len == 0 &&
            scsi_request_sense(req->device, req->sense, SCSI_MAX_SENSE_LEN) == 0) {
            req->sense_len = SCSI_MAX_SENSE_LEN;
        }

        if (req->status == SCSI_STATUS_CHECK_CONDITION && req->sense_len != 0) {
            sense_key = scsi_sense_key(req->sense, req->sense_len);
            switch (sense_key) {
            case SCSI_SENSE_UNIT_ATTENTION:
                should_retry = 1;
                break;
            case SCSI_SENSE_NOT_READY:
                should_retry = 1;
                retry_delay_ms = 250;
                break;
            case SCSI_SENSE_ABORTED_COMMAND:
                should_retry = 1;
                break;
            case SCSI_SENSE_MEDIUM_ERROR:
            default:
                break;
            }
        } else if (req->status == SCSI_STATUS_BUSY ||
                   req->status == SCSI_STATUS_TASK_SET_FULL) {
            should_retry = 1;
            retry_delay_ms = 100;
        }

        attempts--;
        if (should_retry && attempts > 0) {
            req->retries++;
            if (retry_delay_ms != 0) {
                scsi_delay_ms(retry_delay_ms);
            }
            continue;
        }

        req->state = SCSI_REQ_STATE_ERROR;
        req->error = (ret < 0) ? ret : -1;
        link->commands_failed++;
        break;
    }
    
    /* Invoke callback if present */
    if (req->callback) {
        req->callback(req);
    }
    
    return ret;
}

int scsi_execute_sync(scsi_device_t *dev, uint8_t *cdb, uint8_t cdb_len,
                      void *data, uint32_t data_len, uint16_t flags,
                      uint32_t timeout_ms) {
    /*
     * cdb_len is a uint8_t, so a caller can name up to 255 bytes, but
     * req->cdb is SCSI_MAX_CDB_LEN (16).  Every in-tree caller passes a
     * literal 6/10/12/16, but this is an exported symbol and the copy below
     * had no bound, so one caller computing a length from a device-supplied
     * field would overflow the request struct.
     *
     * Reject rather than clamp: silently truncating a CDB does not produce a
     * shorter version of the same command, it produces a different command
     * with the tail of the opcode's operands missing, which the target may
     * well execute.
     */
    if (!cdb || cdb_len == 0 || cdb_len > SCSI_MAX_CDB_LEN) {
        return -1;
    }

    scsi_request_t *req = scsi_request_alloc();
    if (!req) return -1;

    scsi_request_init(req, dev);
    memcpy(req->cdb, cdb, cdb_len);
    req->cdb_len = cdb_len;
    req->data = data;
    req->data_len = data_len;
    req->flags = flags | SCSI_REQ_SYNC;
    req->timeout_ms = timeout_ms;
    
    int ret = scsi_execute(req);
    
    scsi_request_free(req);
    return ret;
}

/*
 * ============================================================
 * Async Queue Management
 * ============================================================
 */

/*
 * Queue a request for async execution
 * The request will be processed when the device becomes available
 */
int scsi_queue_request(scsi_request_t *req) {
    if (!req || !req->device) {
        return -1;
    }
    
    scsi_device_t *dev = req->device;

    if (dev->max_queue_depth != 0 &&
        dev->queue_depth >= dev->max_queue_depth) {
        req->state = SCSI_REQ_STATE_ERROR;
        req->error = -1;
        req->next = NULL;
        if (req->callback) {
            req->callback(req);
        }
        return -1;
    }
    
    req->state = SCSI_REQ_STATE_PENDING;
    req->next = NULL;
    
    /* Add to device queue tail */
    if (dev->queue_tail) {
        dev->queue_tail->next = req;
        dev->queue_tail = req;
    } else {
        dev->queue_head = req;
        dev->queue_tail = req;
    }
    dev->queue_depth++;
    
    /* Try to start processing if queue was empty */
    if (dev->queue_depth == 1) {
        scsi_process_queue(dev);
    }
    
    return 0;
}

/*
 * Process pending requests in device queue
 * Returns: Number of requests started
 */
int scsi_process_queue(scsi_device_t *dev) {
    if (!dev) return 0;
    
    int started = 0;
    
    while (dev->queue_head &&
           (dev->max_queue_depth == 0 || started < (int)dev->max_queue_depth)) {
        scsi_request_t *req = dev->queue_head;
        
        /* Remove from queue head */
        dev->queue_head = req->next;
        if (!dev->queue_head) {
            dev->queue_tail = NULL;
        }
        if (dev->queue_depth > 0) {
            dev->queue_depth--;
        }
        req->next = NULL;
        
        /* Execute the request */
        scsi_execute(req);
        started++;
    }
    
    return started;
}

/*
 * Abort a pending request
 */
int scsi_abort_request(scsi_request_t *req) {
    if (!req || !req->device) {
        return -1;
    }
    
    scsi_device_t *dev = req->device;
    scsi_request_t *prev = NULL;
    
    /* Only abort if pending (not yet started) */
    if (req->state != SCSI_REQ_STATE_PENDING) {
        return -1;  /* Can't abort in-flight requests here */
    }
    
    /* Remove from queue */
    scsi_request_t **pp = &dev->queue_head;
    while (*pp) {
        if (*pp == req) {
            *pp = req->next;
            if (dev->queue_tail == req) {
                dev->queue_tail = prev;
            }
            if (dev->queue_depth > 0) {
                dev->queue_depth--;
            }
            
            req->state = SCSI_REQ_STATE_ERROR;
            req->error = -1;  /* Aborted */
            
            /* Invoke callback */
            if (req->callback) {
                req->callback(req);
            }
            
            return 0;
        }
        prev = *pp;
        pp = &(*pp)->next;
    }
    
    return -1;  /* Not found */
}

/*
 * Mark a request as complete and invoke callback
 */
void scsi_complete_request(scsi_request_t *req, int status) {
    if (!req) return;
    
    req->state = (status == 0) ? SCSI_REQ_STATE_COMPLETE : SCSI_REQ_STATE_ERROR;
    req->error = status;
    
    /* Invoke callback */
    if (req->callback) {
        req->callback(req);
    }
    
    /* Process next queued request */
    if (req->device) {
        scsi_process_queue(req->device);
    }
}

/*
 * ============================================================
 * Standard Commands
 * ============================================================
 */

int scsi_test_unit_ready(scsi_device_t *dev) {
    uint8_t cdb[6];
    scsi_cdb_test_unit_ready(cdb);
    return scsi_execute_sync(dev, cdb, 6, NULL, 0, 0, 5000);
}

int scsi_inquiry(scsi_device_t *dev, struct scsi_inquiry_data *inq) {
    uint8_t cdb[6];
    scsi_cdb_inquiry(cdb, sizeof(struct scsi_inquiry_data));
    return scsi_execute_sync(dev, cdb, 6, inq, sizeof(struct scsi_inquiry_data),
                            SCSI_REQ_READ, 5000);
}

int scsi_read_capacity(scsi_device_t *dev, uint64_t *sectors, uint32_t *sector_size) {
    uint8_t cdb[10];
    struct scsi_read_capacity_10 cap;
    
    scsi_cdb_read_capacity_10(cdb);
    int ret = scsi_execute_sync(dev, cdb, 10, &cap, sizeof(cap), SCSI_REQ_READ, 5000);
    
    if (ret == 0) {
        uint32_t last_lba = scsi_be32((uint8_t *)&cap.lba);

        *sectors = (uint64_t)last_lba + 1U;
        *sector_size = scsi_be32((uint8_t *)&cap.block_size);

        if (last_lba == 0xFFFFFFFFU) {
            uint8_t cdb16[16];
            struct scsi_read_capacity_16 cap16;

            scsi_cdb_read_capacity_16(cdb16, sizeof(cap16));
            ret = scsi_execute_sync(dev, cdb16, 16, &cap16, sizeof(cap16),
                                    SCSI_REQ_READ, 5000);
            if (ret == 0) {
                *sectors = scsi_be64(cap16.last_lba) + 1U;
                *sector_size = scsi_be32(cap16.block_size);
            }
        }
    }
    
    return ret;
}

int scsi_request_sense(scsi_device_t *dev, uint8_t *sense, uint8_t len) {
    uint8_t cdb[6];

    scsi_cdb_request_sense(cdb, len);
    return scsi_execute_sync(dev, cdb, 6, sense, len, 
                            SCSI_REQ_READ | SCSI_REQ_NO_SENSE, 5000);
}

int scsi_start_stop(scsi_device_t *dev, int start, int load_eject) {
    uint8_t cdb[6];

    scsi_cdb_start_stop(cdb, start, load_eject);
    return scsi_execute_sync(dev, cdb, 6, NULL, 0, 0, 30000);
}

/*
 * REPORT LUNS (SPC-3 6.21)
 * Returns list of LUNs available on target
 */
int scsi_report_luns(scsi_device_t *dev, struct scsi_report_luns_data *luns) {
    if (!dev || !luns) return -1;
    
    uint8_t cdb[12];
    memset(cdb, 0, 12);
    cdb[0] = SCSI_CMD_REPORT_LUNS;
    /* Select report type 0 = all LUNs */
    cdb[2] = 0x00;
    /* Allocation length (big-endian, 4 bytes at offset 6) */
    uint32_t alloc_len = sizeof(struct scsi_report_luns_data);
    scsi_put_be32(&cdb[6], alloc_len);
    
    memset(luns, 0, sizeof(struct scsi_report_luns_data));
    
    return scsi_execute_sync(dev, cdb, 12, luns, alloc_len, SCSI_REQ_READ, 10000);
}

int scsi_synchronize_cache(scsi_device_t *dev) {
    uint8_t cdb[10];

    scsi_cdb_sync_cache(cdb, 0, 0);
    return scsi_execute_sync(dev, cdb, 10, NULL, 0, 0, 30000);
}

int scsi_mode_sense(scsi_device_t *dev, uint8_t page, void *buffer, uint16_t len) {
    if (!dev || !buffer || len == 0) {
        return -1;
    }

    if (len <= 0xFFU) {
        uint8_t cdb[6];

        scsi_cdb_mode_sense_6(cdb, page, (uint8_t)len);
        return scsi_execute_sync(dev, cdb, 6, buffer, len, SCSI_REQ_READ, 5000);
    }

    {
        uint8_t cdb[10];

        scsi_cdb_mode_sense_10(cdb, page, len);
        return scsi_execute_sync(dev, cdb, 10, buffer, len, SCSI_REQ_READ, 5000);
    }
}

/*
 * ============================================================
 * Sense Data Parsing
 * ============================================================
 */

int scsi_sense_key(const uint8_t *sense, uint8_t len) {
    if (len < 3) return -1;
    
    uint8_t code = sense[0] & 0x7F;
    if (code == 0x70 || code == 0x71) {
        /* Fixed format */
        return sense[2] & 0x0F;
    } else if (code == 0x72 || code == 0x73) {
        /* Descriptor format */
        return sense[1] & 0x0F;
    }
    return -1;
}

int scsi_sense_asc(const uint8_t *sense, uint8_t len) {
    if (len < 13) return -1;
    
    uint8_t code = sense[0] & 0x7F;
    if (code == 0x70 || code == 0x71) {
        return sense[12];
    } else if (code == 0x72 || code == 0x73) {
        return sense[2];
    }
    return -1;
}

int scsi_sense_ascq(const uint8_t *sense, uint8_t len) {
    if (len < 14) return -1;
    
    uint8_t code = sense[0] & 0x7F;
    if (code == 0x70 || code == 0x71) {
        return sense[13];
    } else if (code == 0x72 || code == 0x73) {
        return sense[3];
    }
    return -1;
}

struct scsi_asc_ascq_entry {
    uint8_t asc;
    uint8_t ascq;
    const char *description;
};

static const struct scsi_asc_ascq_entry scsi_asc_ascq_table[] = {
    { 0x00, 0x00, "No additional sense information" },
    { 0x01, 0x00, "No index/sector signal" },
    { 0x02, 0x00, "No seek complete" },
    { 0x03, 0x00, "Peripheral device write fault" },
    { 0x04, 0x00, "Logical unit not ready, cause not reportable" },
    { 0x04, 0x01, "Logical unit is in process of becoming ready" },
    { 0x04, 0x02, "Logical unit not ready, initializing command required" },
    { 0x04, 0x03, "Logical unit not ready, manual intervention required" },
    { 0x05, 0x00, "Logical unit does not respond to selection" },
    { 0x06, 0x00, "No reference position found" },
    { 0x07, 0x00, "Multiple peripheral devices selected" },
    { 0x08, 0x00, "Logical unit communication failure" },
    { 0x08, 0x01, "Logical unit communication time-out" },
    { 0x09, 0x00, "Track following error" },
    { 0x0A, 0x00, "Error log overflow" },
    { 0x0B, 0x00, "Warning" },
    { 0x0C, 0x00, "Write error" },
    { 0x11, 0x00, "Unrecovered read error" },
    { 0x14, 0x00, "Recorded entity not found" },
    { 0x15, 0x00, "Random positioning error" },
    { 0x17, 0x00, "Recovered data with no error correction applied" },
    { 0x18, 0x00, "Recovered data with error correction applied" },
    { 0x1A, 0x00, "Parameter list length error" },
    { 0x1B, 0x00, "Synchronous data transfer error" },
    { 0x1D, 0x00, "Miscompare during verify operation" },
    { 0x20, 0x00, "Invalid command operation code" },
    { 0x21, 0x00, "Logical block address out of range" },
    { 0x24, 0x00, "Invalid field in CDB" },
    { 0x25, 0x00, "Logical unit not supported" },
    { 0x26, 0x00, "Invalid field in parameter list" },
    { 0x27, 0x00, "Write protected" },
    { 0x28, 0x00, "Not ready to ready change, medium may have changed" },
    { 0x29, 0x00, "Power on, reset, or bus device reset occurred" },
    { 0x2A, 0x00, "Parameters changed" },
    { 0x2A, 0x01, "Mode parameters changed" },
    { 0x2A, 0x02, "Log parameters changed" },
    { 0x2A, 0x03, "Reservations preempted" },
    { 0x2C, 0x00, "Command sequence error" },
    { 0x30, 0x00, "Incompatible medium installed" },
    { 0x31, 0x00, "Medium format corrupted" },
    { 0x32, 0x00, "No defect spare location available" },
    { 0x3A, 0x00, "Medium not present" },
    { 0x3A, 0x01, "Medium not present - tray closed" },
    { 0x3A, 0x02, "Medium not present - tray open" },
    { 0x3B, 0x11, "Medium load or eject failed" },
    { 0x3D, 0x00, "Invalid bits in identify message" },
    { 0x3E, 0x00, "Logical unit has not self-configured yet" },
    { 0x3F, 0x00, "Target operating conditions have changed" },
    { 0x3F, 0x01, "Microcode has been changed" },
    { 0x3F, 0x02, "Changed operating definition" },
    { 0x3F, 0x03, "Inquiry data has changed" },
    { 0x40, 0x00, "RAM failure" },
    { 0x41, 0x00, "Data path failure" },
    { 0x42, 0x00, "Power-on or self-test failure" },
    { 0x43, 0x00, "Message error" },
    { 0x44, 0x00, "Internal target failure" },
    { 0x45, 0x00, "Select or reselect failure" },
    { 0x46, 0x00, "Unsuccessful soft reset" },
    { 0x47, 0x00, "SCSI parity error" },
    { 0x48, 0x00, "Initiator detected error message received" },
    { 0x49, 0x00, "Invalid message error" },
    { 0x4A, 0x00, "Command phase error" },
    { 0x4B, 0x00, "Data phase error" },
    { 0x4C, 0x00, "Logical unit failed self-configuration" },
    { 0x4E, 0x00, "Overlapped commands attempted" },
};

static const char *scsi_asc_ascq_lookup(uint8_t asc, uint8_t ascq) {
    size_t i;
    for (i = 0; i < sizeof(scsi_asc_ascq_table) / sizeof(scsi_asc_ascq_table[0]); i++) {
        if (scsi_asc_ascq_table[i].asc == asc && scsi_asc_ascq_table[i].ascq == ascq) {
            return scsi_asc_ascq_table[i].description;
        }
    }
    return NULL;
}

const char *scsi_sense_string(uint8_t key, uint8_t asc, uint8_t ascq) {
    const char *asc_str = scsi_asc_ascq_lookup(asc, ascq);
    if (asc_str && (asc != 0 || ascq != 0)) {
        return asc_str;
    }
    
    static const char *keys[] = {
        "No Sense",
        "Recovered Error",
        "Not Ready",
        "Medium Error",
        "Hardware Error",
        "Illegal Request",
        "Unit Attention",
        "Data Protect",
        "Blank Check",
        "Vendor Specific",
        "Copy Aborted",
        "Aborted Command",
        "Reserved",
        "Volume Overflow",
        "Miscompare",
        "Completed"
    };
    
    if (key < 16) {
        return keys[key];
    }
    return "Unknown";
}

/*
 * ============================================================
 * CDB Construction Helpers
 * ============================================================
 */

void scsi_cdb_test_unit_ready(uint8_t *cdb) {
    memset(cdb, 0, 6);
    cdb[0] = SCSI_CMD_TEST_UNIT_READY;
}

void scsi_cdb_inquiry(uint8_t *cdb, uint8_t len) {
    memset(cdb, 0, 6);
    cdb[0] = SCSI_CMD_INQUIRY;
    cdb[4] = len;
}

void scsi_cdb_read_capacity_10(uint8_t *cdb) {
    memset(cdb, 0, 10);
    cdb[0] = SCSI_CMD_READ_CAPACITY_10;
}

void scsi_cdb_read_10(uint8_t *cdb, uint32_t lba, uint16_t count) {
    memset(cdb, 0, 10);
    cdb[0] = SCSI_CMD_READ_10;
    scsi_put_be32(&cdb[2], lba);
    scsi_put_be16(&cdb[7], count);
}

void scsi_cdb_write_10(uint8_t *cdb, uint32_t lba, uint16_t count) {
    memset(cdb, 0, 10);
    cdb[0] = SCSI_CMD_WRITE_10;
    scsi_put_be32(&cdb[2], lba);
    scsi_put_be16(&cdb[7], count);
}

void scsi_cdb_read_16(uint8_t *cdb, uint64_t lba, uint32_t count) {
    memset(cdb, 0, 16);
    cdb[0] = SCSI_CMD_READ_16;
    scsi_put_be64(&cdb[2], lba);
    scsi_put_be32(&cdb[10], count);
}

void scsi_cdb_write_16(uint8_t *cdb, uint64_t lba, uint32_t count) {
    memset(cdb, 0, 16);
    cdb[0] = SCSI_CMD_WRITE_16;
    scsi_put_be64(&cdb[2], lba);
    scsi_put_be32(&cdb[10], count);
}

void scsi_cdb_request_sense(uint8_t *cdb, uint8_t len) {
    memset(cdb, 0, 6);
    cdb[0] = SCSI_CMD_REQUEST_SENSE;
    cdb[4] = len;
}

void scsi_cdb_mode_sense_6(uint8_t *cdb, uint8_t page, uint8_t len) {
    memset(cdb, 0, 6);
    cdb[0] = SCSI_CMD_MODE_SENSE_6;
    cdb[2] = (uint8_t)(page & 0x3FU);
    cdb[4] = len;
}

void scsi_cdb_mode_sense_10(uint8_t *cdb, uint8_t page, uint16_t len) {
    memset(cdb, 0, 10);
    cdb[0] = SCSI_CMD_MODE_SENSE_10;
    cdb[2] = (uint8_t)(page & 0x3FU);
    scsi_put_be16(&cdb[7], len);
}

void scsi_cdb_start_stop(uint8_t *cdb, int start, int load_eject) {
    memset(cdb, 0, 6);
    cdb[0] = SCSI_CMD_START_STOP_UNIT;
    cdb[4] = (uint8_t)((load_eject ? 0x02 : 0) | (start ? 0x01 : 0));
}

void scsi_cdb_sync_cache(uint8_t *cdb, uint32_t lba, uint16_t count) {
    memset(cdb, 0, 10);
    cdb[0] = SCSI_CMD_SYNCHRONIZE_CACHE;
    scsi_put_be32(&cdb[2], lba);
    scsi_put_be16(&cdb[7], count);
}

/*
 * ============================================================
 * Discovery - Bus Scanning (L620)
 * ============================================================
 */

/*
 * Probe a single LUN on a given target
 * Returns: 0 on success (device found and registered), -1 on failure
 */
int scsi_probe_lun(scsi_link_t *link, uint8_t bus, uint8_t target, uint16_t lun) {
    if (!link || !link->execute) {
        return -1;
    }
    
    /* Allocate temporary device for probing */
    scsi_device_t *dev = scsi_device_alloc();
    if (!dev) {
        return -1;
    }
    
    dev->bus = bus;
    dev->target = target;
    dev->lun = lun;
    dev->link = link;
    
    /* Issue TEST UNIT READY to check if target responds */
    scsi_request_t *req = scsi_request_alloc();
    if (!req) {
        scsi_device_free(dev);
        return -1;
    }
    
    scsi_request_init(req, dev);
    scsi_cdb_test_unit_ready(req->cdb);
    req->cdb_len = 6;
    req->flags = SCSI_REQ_SYNC | SCSI_REQ_QUIET;
    req->timeout_ms = 2000;  /* Short timeout for probe */
    req->retries = 1;
    
    int ret = link->execute(link, req);
    
    /* Allow CHECK CONDITION (device exists but may need attention) */
    if (ret < 0 && req->status != SCSI_STATUS_CHECK_CONDITION) {
        scsi_request_free(req);
        scsi_device_free(dev);
        return -1;
    }
    
    /* Issue INQUIRY to get device type and identity */
    scsi_request_init(req, dev);
    struct scsi_inquiry_data inq;
    memset(&inq, 0, sizeof(inq));
    
    scsi_cdb_inquiry(req->cdb, SCSI_INQUIRY_LEN);
    req->cdb_len = 6;
    req->data = &inq;
    req->data_len = SCSI_INQUIRY_LEN;
    req->flags = SCSI_REQ_SYNC | SCSI_REQ_READ;
    req->timeout_ms = 5000;
    req->retries = 2;
    
    ret = link->execute(link, req);
    scsi_request_free(req);
    
    if (ret < 0) {
        scsi_device_free(dev);
        return -1;
    }
    
    /* Check device type - 0x1F means no device */
    uint8_t dtype = inq.device_type & 0x1F;
    if (dtype == SCSI_TYPE_NO_DEVICE) {
        scsi_device_free(dev);
        return -1;
    }
    
    /* Check peripheral qualifier - 0x3 means not connected */
    uint8_t pq = (inq.device_type >> 5) & 0x7;
    if (pq == 0x3) {
        scsi_device_free(dev);
        return -1;
    }
    
    /* Populate device info from INQUIRY */
    dev->type = dtype;
    dev->removable = (inq.rmb & 0x80) ? 1 : 0;
    dev->scsi_version = inq.version;
    if (dev->removable) {
        dev->flags |= SCSI_DEV_REMOVABLE;
    }
    
    /* Copy vendor/product/revision (space-padded in INQUIRY, need null-term) */
    memcpy(dev->vendor, inq.vendor, 8);
    dev->vendor[8] = '\0';
    /* Trim trailing spaces */
    for (int i = 7; i >= 0 && dev->vendor[i] == ' '; i--) {
        dev->vendor[i] = '\0';
    }
    
    memcpy(dev->product, inq.product, 16);
    dev->product[16] = '\0';
    for (int i = 15; i >= 0 && dev->product[i] == ' '; i--) {
        dev->product[i] = '\0';
    }
    
    memcpy(dev->revision, inq.revision, 4);
    dev->revision[4] = '\0';
    for (int i = 3; i >= 0 && dev->revision[i] == ' '; i--) {
        dev->revision[i] = '\0';
    }
    
    /*
     * For block devices, get capacity.
     *
     * A removable device with an empty slot answers INQUIRY perfectly well
     * but fails READ CAPACITY with "medium not present" (sense key 2, ASC
     * 0x3A), so a successful capacity read -- not the INQUIRY -- is what
     * tells us there is anything to talk to.  An all-in-one card reader
     * presents one LUN per slot and most of them are usually empty; a
     * CD/DVD drive with no disc is the same case.
     */
    int have_media = 1;

    if (dtype == SCSI_TYPE_DISK || dtype == SCSI_TYPE_ROM ||
        dtype == SCSI_TYPE_OPTICAL || dtype == SCSI_TYPE_RBC) {
        uint64_t sectors = 0;
        uint32_t sector_sz = 0;
        if (scsi_read_capacity(dev, &sectors, &sector_sz) == 0 && sectors > 0) {
            dev->capacity = sectors;
            dev->sector_size = sector_sz;
        } else {
            /* Default to 512-byte sectors if capacity read fails */
            dev->sector_size = 512;
            dev->capacity = 0;
            have_media = 0;
        }
    }

    dev->online = 1;
    /* Report what the capacity read actually found rather than assuming;
     * claiming media on an empty slot makes the block layer issue reads
     * that cannot succeed. */
    dev->media_present = (uint8_t)have_media;
    dev->flags |= SCSI_DEV_ONLINE;
    
    /* Register device */
    if (scsi_device_register(dev) < 0) {
        scsi_device_free(dev);
        return -1;
    }

    scsi_auto_attach(dev);

    return 0;
}

/*
 * Scan a SCSI bus for all targets and LUNs
 * Returns: Number of devices found
 */
int scsi_scan_bus(scsi_link_t *link, uint8_t bus) {
    if (!link) {
        return -1;
    }
    
    // kprintf("SCSI: Scanning bus %d via '%s'\n", bus, link->name[0] ? link->name : "unknown");
    
    int devices_found = 0;
    
    /*
     * Standard SCSI bus scan:
     * - Enumerate targets 0 to SCSI_MAX_TARGETS-1
     * - For each target, probe LUN 0 first
     * - If LUN 0 responds, optionally probe more LUNs (REPORT LUNS in L621)
     */
    for (uint8_t target = 0; target < link->max_targets; target++) {
        /* Probe LUN 0 first */
        if (scsi_probe_lun(link, bus, target, 0) == 0) {
            devices_found++;
            
            /*
             * LUN 0 found - use REPORT LUNS to discover additional LUNs
             * If REPORT LUNS fails (not supported), continue with just LUN 0
             */
            scsi_device_t *lun0_dev = scsi_device_lookup(bus, target, 0);
            if (lun0_dev) {
                struct scsi_report_luns_data luns_data;
                if (scsi_report_luns(lun0_dev, &luns_data) == 0) {
                    uint32_t list_len = scsi_be32((uint8_t*)&luns_data.length);
                    uint32_t num_luns = list_len / 8;  /* Each LUN is 8 bytes */

                    /* Cap at reasonable limit */
                    if (num_luns > SCSI_MAX_LUNS_RESPONSE) {
                        num_luns = SCSI_MAX_LUNS_RESPONSE;
                    }

                    /* Probe each reported LUN (skip LUN 0, already probed) */
                    for (uint32_t i = 0; i < num_luns; i++) {
                        uint16_t lun_num;

                        if (scsi_lun_from_report_desc(
                                (const uint8_t *)&luns_data.luns[i],
                                &lun_num) != 0) {
                            continue;  /* Multi-level address, not a flat LUN */
                        }

                        if (lun_num == 0) {
                            continue;  /* Already probed */
                        }
                        if (scsi_probe_lun(link, bus, target, lun_num) == 0) {
                            devices_found++;
                        }
                    }
                }

                /*
                 * REPORT LUNS is advisory, not exhaustive.  USB Bulk-Only
                 * multi-slot card readers declare their slot count through
                 * GET MAX LUN (which is what set link->max_luns) and then
                 * either STALL REPORT LUNS or answer it with LUN 0 alone,
                 * so keying the sweep off REPORT LUNS alone leaves every
                 * slot but the first invisible.  Probe the declared range
                 * as well, skipping addresses already registered above.
                 */
                for (uint16_t lun = 1; lun < link->max_luns; lun++) {
                    if (scsi_device_lookup(bus, target, lun)) {
                        continue;
                    }
                    if (scsi_probe_lun(link, bus, target, lun) == 0) {
                        devices_found++;
                    }
                }
            }
        }
    }
    
    // kprintf("SCSI: Bus %d scan complete, %d device(s) found\n", bus, devices_found);
    
    return devices_found;
}
