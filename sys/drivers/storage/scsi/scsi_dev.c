/*
 * scsi_dev.c - Unified SCSI Device Driver
 *
 * High-level driver for all SCSI device types.
 * No artificial sd/sr split - just SCSI devices with type-specific handling.
 */

#include "scsi.h"
#include "../blkdev.h"
#include "../../../kern/console.h"
#include <string.h>
#include <stdio.h>

/*
 * ============================================================
 * SCSI Device Private Data
 * ============================================================
 */

typedef struct scsi_blk_dev {
    scsi_device_t *scsi_dev;    /* Underlying SCSI device */
    blkdev_t blkdev;            /* Block device interface */
    uint32_t dev_num;           /* Device number (for naming) */
    struct scsi_blk_dev *next;  /* Linked list */
} scsi_blk_dev_t;

/* Global state */
static scsi_blk_dev_t *scsi_dev_list = NULL;
static uint32_t scsi_dev_count = 0;
static scsi_blk_dev_t scsi_dev_pool[32];

/*
 * ============================================================
 * Device Type Constants
 * ============================================================
 */
#define CD_SECTOR_SIZE      2048    /* Standard data CD */

/*
 * ============================================================
 * Block Device Callbacks
 * ============================================================
 */

static int scsi_blk_read(blkdev_t *dev, uint64_t sector, uint32_t count, void *buffer) {
    scsi_blk_dev_t *sbd = (scsi_blk_dev_t *)dev->priv;
    if (!sbd || !sbd->scsi_dev) return -1;
    
    scsi_device_t *scsi = sbd->scsi_dev;
    uint8_t cdb[10];
    
    /* Build READ(10) CDB */
    scsi_cdb_read_10(cdb, (uint32_t)sector, (uint16_t)count);
    
    int ret = scsi_execute_sync(scsi, cdb, 10, buffer,
                                 count * dev->sector_size,
                                 SCSI_REQ_READ, 60000);
    
    return (ret >= 0) ? (int)count : -1;
}

static int scsi_blk_write(blkdev_t *dev, uint64_t sector, uint32_t count, const void *buffer) {
    scsi_blk_dev_t *sbd = (scsi_blk_dev_t *)dev->priv;
    if (!sbd || !sbd->scsi_dev) return -1;
    
    scsi_device_t *scsi = sbd->scsi_dev;
    
    /* CD-ROMs are typically read-only */
    if (scsi->type == SCSI_TYPE_ROM) {
        return -1;
    }
    
    uint8_t cdb[10];
    
    /* Build WRITE(10) CDB */
    scsi_cdb_write_10(cdb, (uint32_t)sector, (uint16_t)count);
    
    int ret = scsi_execute_sync(scsi, cdb, 10, (void *)buffer,
                                 count * dev->sector_size,
                                 SCSI_REQ_WRITE, 30000);
    
    return (ret >= 0) ? (int)count : -1;
}

/*
 * ============================================================
 * CD-ROM Specific Commands
 * ============================================================
 */

/* Read Table of Contents */
int scsi_read_toc(scsi_device_t *dev, void *buffer, uint16_t buflen) {
    if (!dev) return -1;
    
    uint8_t cdb[10] = {0};
    cdb[0] = 0x43;  /* READ TOC/PMA/ATIP */
    cdb[1] = 0x02;  /* MSF format */
    cdb[6] = 0;     /* Starting track */
    cdb[7] = (buflen >> 8) & 0xFF;
    cdb[8] = buflen & 0xFF;
    
    return scsi_execute_sync(dev, cdb, 10, buffer, buflen,
                              SCSI_REQ_READ, 30000);
}

/* Lock/unlock door */
int scsi_lock_door(scsi_device_t *dev, int lock) {
    if (!dev) return -1;
    
    uint8_t cdb[6] = {0};
    cdb[0] = 0x1E;  /* PREVENT/ALLOW MEDIUM REMOVAL */
    cdb[4] = lock ? 0x01 : 0x00;
    
    return scsi_execute_sync(dev, cdb, 6, NULL, 0, 0, 10000);
}

/*
 * ============================================================
 * Unified Device API
 * ============================================================
 */

/*
 * Attach any SCSI device as a block device
 */
int scsi_dev_attach(scsi_device_t *scsi_dev) {
    if (!scsi_dev) return -1;
    
    /* Only attach block-capable device types */
    if (scsi_dev->type != SCSI_TYPE_DISK && 
        scsi_dev->type != SCSI_TYPE_ROM &&
        scsi_dev->type != SCSI_TYPE_OPTICAL &&
        scsi_dev->type != SCSI_TYPE_WORM) {
        return -1;  /* Not a block device type */
    }
    
    if (scsi_dev_count >= 32) {
        kprint("scsi: too many devices\n");
        return -1;
    }
    
    scsi_blk_dev_t *sbd = &scsi_dev_pool[scsi_dev_count];
    memset(sbd, 0, sizeof(*sbd));
    
    sbd->scsi_dev = scsi_dev;
    sbd->dev_num = scsi_dev_count;
    
    /* Create device name: scsi0, scsi1, etc. */
    sprintf(sbd->blkdev.name, "scsi%u", scsi_dev_count);
    
    /* Set sector size based on device type */
    if (scsi_dev->type == SCSI_TYPE_ROM || scsi_dev->type == SCSI_TYPE_OPTICAL) {
        sbd->blkdev.sector_size = CD_SECTOR_SIZE;
    } else {
        sbd->blkdev.sector_size = scsi_dev->sector_size ? scsi_dev->sector_size : 512;
    }
    
    sbd->blkdev.total_sectors = scsi_dev->capacity;
    sbd->blkdev.priv = sbd;
    sbd->blkdev.read = scsi_blk_read;
    sbd->blkdev.write = scsi_blk_write;
    
    /* Register with block device subsystem */
    blkdev_register(&sbd->blkdev);
    
    /* Add to list */
    sbd->next = scsi_dev_list;
    scsi_dev_list = sbd;
    scsi_dev_count++;
    
    const char *type_str = "unknown";
    switch (scsi_dev->type) {
    case SCSI_TYPE_DISK:    type_str = "disk"; break;
    case SCSI_TYPE_ROM:     type_str = "cdrom"; break;
    case SCSI_TYPE_OPTICAL: type_str = "optical"; break;
    case SCSI_TYPE_WORM:    type_str = "worm"; break;
    }
    
    char log_buf[128];
    sprintf(log_buf, "scsi: attached %s (%s) [%s %s]\n",
            sbd->blkdev.name,
            type_str,
            scsi_dev->vendor,
            scsi_dev->product);
    kprint(log_buf);
    
    return 0;
}

/*
 * Detach a SCSI device
 */
int scsi_dev_detach(scsi_device_t *scsi_dev) {
    scsi_blk_dev_t **pp = &scsi_dev_list;
    
    while (*pp) {
        if ((*pp)->scsi_dev == scsi_dev) {
            scsi_blk_dev_t *sbd = *pp;
            *pp = sbd->next;
            
            char log_buf[64];
            sprintf(log_buf, "scsi: detached %s\n", sbd->blkdev.name);
            kprint(log_buf);
            return 0;
        }
        pp = &(*pp)->next;
    }
    
    return -1;
}

/*
 * Get SCSI device by name
 */
scsi_blk_dev_t *scsi_dev_lookup(const char *name) {
    for (scsi_blk_dev_t *sbd = scsi_dev_list; sbd; sbd = sbd->next) {
        if (strcmp(sbd->blkdev.name, name) == 0) {
            return sbd;
        }
    }
    return NULL;
}

/*
 * Initialize SCSI device subsystem
 */
void scsi_dev_init(void) {
    kprint("scsi: unified SCSI device driver initialized\n");
}
