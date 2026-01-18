/*
 * sd.c - SCSI Disk Driver
 *
 * High-level driver for SCSI direct-access devices (TYPE_DISK).
 * Bridges SCSI mid-layer to block device subsystem.
 */

#include "scsi.h"
#include "../blkdev.h"
#include "../../../kern/console.h"
#include <string.h>

/*
 * ============================================================
 * SCSI Disk Private Data
 * ============================================================
 */

typedef struct sd_device {
    scsi_device_t *scsi_dev;    /* Underlying SCSI device */
    blkdev_t blkdev;            /* Block device interface */
    uint32_t disk_num;          /* Disk number (for naming) */
    struct sd_device *next;     /* Linked list */
} sd_device_t;

/* Global state */
static sd_device_t *sd_list = NULL;
static uint32_t sd_count = 0;

/*
 * ============================================================
 * Block Device Callbacks
 * ============================================================
 */

static int sd_read(blkdev_t *dev, uint64_t sector, uint32_t count, void *buffer) {
    sd_device_t *sd = (sd_device_t *)dev->priv;
    if (!sd || !sd->scsi_dev) return -1;
    
    scsi_device_t *scsi = sd->scsi_dev;
    uint8_t cdb[10];
    
    /* Build READ(10) CDB */
    scsi_cdb_read_10(cdb, (uint32_t)sector, (uint16_t)count);
    
    int ret = scsi_execute_sync(scsi, cdb, 10, buffer,
                                 count * scsi->sector_size,
                                 SCSI_REQ_READ, 30000);
    
    return (ret >= 0) ? (int)count : -1;
}

static int sd_write(blkdev_t *dev, uint64_t sector, uint32_t count, const void *buffer) {
    sd_device_t *sd = (sd_device_t *)dev->priv;
    if (!sd || !sd->scsi_dev) return -1;
    
    scsi_device_t *scsi = sd->scsi_dev;
    uint8_t cdb[10];
    
    /* Build WRITE(10) CDB */
    scsi_cdb_write_10(cdb, (uint32_t)sector, (uint16_t)count);
    
    int ret = scsi_execute_sync(scsi, cdb, 10, (void *)buffer,
                                 count * scsi->sector_size,
                                 SCSI_REQ_WRITE, 30000);
    
    return (ret >= 0) ? (int)count : -1;
}

/*
 * ============================================================
 * SD Driver API
 * ============================================================
 */

/*
 * Attach a SCSI disk device
 * Called when discovery finds a TYPE_DISK device
 */
int sd_attach(scsi_device_t *scsi_dev) {
    if (!scsi_dev || scsi_dev->type != SCSI_TYPE_DISK) {
        return -1;
    }
    
    /* Allocate sd_device */
    /* Note: In production, use UMA zone allocator */
    static sd_device_t sd_pool[16];
    if (sd_count >= 16) {
        kprintf("sd: too many disks\n");
        return -1;
    }
    
    sd_device_t *sd = &sd_pool[sd_count];
    memset(sd, 0, sizeof(*sd));
    
    sd->scsi_dev = scsi_dev;
    sd->disk_num = sd_count;
    
    /* Initialize block device */
    snprintf(sd->blkdev.name, sizeof(sd->blkdev.name), "sd%c", 'a' + sd_count);
    sd->blkdev.sector_size = scsi_dev->sector_size ? scsi_dev->sector_size : 512;
    sd->blkdev.total_sectors = scsi_dev->capacity;
    sd->blkdev.priv = sd;
    sd->blkdev.read = sd_read;
    sd->blkdev.write = sd_write;
    
    /* Register with block device subsystem */
    blkdev_register(&sd->blkdev);
    
    /* Add to list */
    sd->next = sd_list;
    sd_list = sd;
    sd_count++;
    
    kprintf("sd: attached %s [%s %s] %lluMB\n",
            sd->blkdev.name,
            scsi_dev->vendor,
            scsi_dev->product,
            (unsigned long long)(scsi_dev->capacity * sd->blkdev.sector_size / (1024 * 1024)));
    
    return 0;
}

/*
 * Detach a SCSI disk device
 */
int sd_detach(scsi_device_t *scsi_dev) {
    sd_device_t **pp = &sd_list;
    
    while (*pp) {
        if ((*pp)->scsi_dev == scsi_dev) {
            sd_device_t *sd = *pp;
            *pp = sd->next;
            
            /* Unregister from block device subsystem */
            /* blkdev_unregister(&sd->blkdev); */
            
            kprintf("sd: detached %s\n", sd->blkdev.name);
            /* Note: Don't actually free from static pool */
            return 0;
        }
        pp = &(*pp)->next;
    }
    
    return -1;  /* Not found */
}

/*
 * Get SD device by name
 */
sd_device_t *sd_lookup(const char *name) {
    for (sd_device_t *sd = sd_list; sd; sd = sd->next) {
        if (strcmp(sd->blkdev.name, name) == 0) {
            return sd;
        }
    }
    return NULL;
}

/*
 * Initialize sd driver
 */
void sd_init(void) {
    kprintf("sd: SCSI disk driver initialized\n");
}
