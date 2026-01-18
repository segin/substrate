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

#include "scsi.h"
#include "../../../kern/console.h"
#include <stdio.h>
#include <string.h>

/*
 * ============================================================
 * Static Storage
 * ============================================================
 */

/* Device Registry */
static scsi_device_t *scsi_device_list = NULL;
static int scsi_device_count = 0;

/* Transport Registry */
#define SCSI_MAX_LINKS 8
static scsi_link_t *scsi_links[SCSI_MAX_LINKS];
static int scsi_link_count = 0;

/* Request Pool */
#define SCSI_REQUEST_POOL_SIZE 32
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
    
    kprint("SCSI: Mid-layer initialized (");
    char buf[32];
    sprintf(buf, "%d", SCSI_REQUEST_POOL_SIZE);
    kprint(buf);
    kprint(" request slots, ");
    sprintf(buf, "%d", SCSI_DEVICE_POOL_SIZE);
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
    
    scsi_links[scsi_link_count++] = link;
    
    kprint("SCSI: Registered transport '");
    kprint(link->name ? link->name : "unknown");
    kprint("'\n");
    
    return 0;
}

void scsi_unregister_link(scsi_link_t *link) {
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
    if (!scsi_free_devices) {
        kprint("SCSI: Device pool exhausted\n");
        return NULL;
    }
    
    scsi_device_t *dev = scsi_free_devices;
    scsi_free_devices = dev->next;
    
    memset(dev, 0, sizeof(scsi_device_t));
    dev->max_queue_depth = 1;  /* Default single-command queue */
    
    return dev;
}

void scsi_device_free(scsi_device_t *dev) {
    if (!dev) return;
    
    /* Ensure device is not in the registry */
    scsi_device_unregister(dev);
    
    /* Return to pool */
    memset(dev, 0, sizeof(scsi_device_t));
    dev->next = scsi_free_devices;
    scsi_free_devices = dev;
}

int scsi_device_register(scsi_device_t *dev) {
    if (!dev) return -1;
    
    /* Check for duplicates */
    scsi_device_t *existing = scsi_device_lookup(dev->bus, dev->target, dev->lun);
    if (existing) {
        kprint("SCSI: Device already registered at ");
        char buf[32];
        sprintf(buf, "%d:%d:%d\n", dev->bus, dev->target, dev->lun);
        kprint(buf);
        return -1;
    }
    
    /* Add to list */
    dev->next = scsi_device_list;
    scsi_device_list = dev;
    scsi_device_count++;
    
    /* Log registration */
    char buf[128];
    sprintf(buf, "SCSI: Registered device %d:%d:%d type=0x%02x '%s %s'\n",
            dev->bus, dev->target, dev->lun, dev->type,
            dev->vendor, dev->product);
    kprint(buf);
    
    return 0;
}

void scsi_device_unregister(scsi_device_t *dev) {
    if (!dev) return;
    
    scsi_device_t **pp = &scsi_device_list;
    while (*pp) {
        if (*pp == dev) {
            *pp = dev->next;
            dev->next = NULL;
            scsi_device_count--;
            return;
        }
        pp = &(*pp)->next;
    }
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
    if (!scsi_free_requests) {
        kprint("SCSI: Request pool exhausted\n");
        return NULL;
    }
    
    scsi_request_t *req = scsi_free_requests;
    scsi_free_requests = req->next;
    
    memset(req, 0, sizeof(scsi_request_t));
    req->state = SCSI_REQ_STATE_PENDING;
    req->timeout_ms = 30000;  /* 30 second default */
    req->retries = 3;
    
    return req;
}

void scsi_request_free(scsi_request_t *req) {
    if (!req) return;
    
    memset(req, 0, sizeof(scsi_request_t));
    req->state = SCSI_REQ_STATE_FREE;
    req->next = scsi_free_requests;
    scsi_free_requests = req;
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
    req->retries = 3;
    req->callback = NULL;
    req->callback_arg = NULL;
}

/*
 * ============================================================
 * Command Execution
 * ============================================================
 */

int scsi_execute(scsi_request_t *req) {
    if (!req || !req->device || !req->device->link) {
        return -1;
    }
    
    scsi_link_t *link = req->device->link;
    
    req->state = SCSI_REQ_STATE_ACTIVE;
    link->commands_issued++;
    
    /* Execute via transport */
    int ret = link->execute(link, req);
    
    if (ret < 0) {
        req->state = SCSI_REQ_STATE_ERROR;
        req->error = ret;
        link->commands_failed++;
    } else {
        req->state = SCSI_REQ_STATE_COMPLETE;
        
        /* Update statistics */
        if (req->flags & SCSI_REQ_READ) {
            link->bytes_read += req->data_xfer;
        } else if (req->flags & SCSI_REQ_WRITE) {
            link->bytes_written += req->data_xfer;
        }
    }
    
    /* Handle CHECK CONDITION */
    if (req->status == SCSI_STATUS_CHECK_CONDITION && 
        !(req->flags & SCSI_REQ_NO_SENSE) &&
        req->sense_len == 0) {
        /* Auto-request sense */
        scsi_request_sense(req->device, req->sense, SCSI_MAX_SENSE_LEN);
        req->sense_len = 18;  /* Fixed sense minimum */
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
        *sectors = scsi_be32((uint8_t*)&cap.lba) + 1;
        *sector_size = scsi_be32((uint8_t*)&cap.block_size);
    }
    
    return ret;
}

int scsi_request_sense(scsi_device_t *dev, uint8_t *sense, uint8_t len) {
    uint8_t cdb[6] = {SCSI_CMD_REQUEST_SENSE, 0, 0, 0, len, 0};
    return scsi_execute_sync(dev, cdb, 6, sense, len, 
                            SCSI_REQ_READ | SCSI_REQ_NO_SENSE, 5000);
}

int scsi_start_stop(scsi_device_t *dev, int start, int load_eject) {
    uint8_t cdb[6] = {SCSI_CMD_START_STOP_UNIT, 0, 0, 0, 0, 0};
    cdb[4] = (load_eject ? 0x02 : 0) | (start ? 0x01 : 0);
    return scsi_execute_sync(dev, cdb, 6, NULL, 0, 0, 30000);
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

const char *scsi_sense_string(uint8_t key, uint8_t asc, uint8_t ascq) {
    (void)asc; (void)ascq;  /* TODO: Full ASC/ASCQ table */
    
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
        "Reserved"
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

/*
 * ============================================================
 * Discovery (Stubs - Full implementation in subsequent commits)
 * ============================================================
 */

int scsi_scan_bus(scsi_link_t *link, uint8_t bus) {
    (void)link; (void)bus;
    /* Implemented in L620 */
    return 0;
}

int scsi_probe_lun(scsi_link_t *link, uint8_t bus, uint8_t target, uint16_t lun) {
    (void)link; (void)bus; (void)target; (void)lun;
    /* Implemented in L621 */
    return 0;
}
