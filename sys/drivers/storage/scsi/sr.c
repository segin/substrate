/*
 * sr.c - SCSI CD-ROM Driver
 *
 * High-level driver for SCSI CD-ROM/DVD devices (TYPE_CDROM).
 * Provides block device interface with CD-specific commands.
 */

#include "scsi.h"
#include "../blkdev.h"
#include "../../../kern/console.h"
#include <string.h>

/*
 * ============================================================
 * CD-ROM Sector Sizes
 * ============================================================
 */
#define CD_SECTOR_SIZE      2048    /* Standard data CD */
#define CD_SECTOR_SIZE_RAW  2352    /* Raw audio/data CD */

/*
 * ============================================================
 * SCSI CD-ROM Private Data
 * ============================================================
 */

typedef struct sr_device {
    scsi_device_t *scsi_dev;    /* Underlying SCSI device */
    blkdev_t blkdev;            /* Block device interface */
    uint32_t drive_num;         /* Drive number (for naming) */
    uint8_t  tray_open;         /* Tray state */
    uint8_t  media_present;     /* Media loaded */
    struct sr_device *next;     /* Linked list */
} sr_device_t;

/* Global state */
static sr_device_t *sr_list = NULL;
static uint32_t sr_count = 0;

/*
 * ============================================================
 * CD-ROM Specific Commands
 * ============================================================
 */

/* Read Table of Contents */
int sr_read_toc(sr_device_t *sr, void *buffer, uint16_t buflen) {
    if (!sr || !sr->scsi_dev) return -1;
    
    uint8_t cdb[10] = {0};
    cdb[0] = 0x43;  /* READ TOC/PMA/ATIP */
    cdb[1] = 0x02;  /* MSF format */
    cdb[6] = 0;     /* Starting track */
    cdb[7] = (buflen >> 8) & 0xFF;
    cdb[8] = buflen & 0xFF;
    
    return scsi_execute_sync(sr->scsi_dev, cdb, 10, buffer, buflen,
                              SCSI_REQ_READ, 30000);
}

/* Eject/Load tray */
int sr_start_stop(sr_device_t *sr, int load, int eject) {
    if (!sr || !sr->scsi_dev) return -1;
    
    uint8_t cdb[6] = {0};
    cdb[0] = SCSI_CMD_START_STOP;
    cdb[4] = (load ? 0x01 : 0) | (eject ? 0x02 : 0);
    
    return scsi_execute_sync(sr->scsi_dev, cdb, 6, NULL, 0, 0, 30000);
}

/* Lock/unlock door */
int sr_lock_door(sr_device_t *sr, int lock) {
    if (!sr || !sr->scsi_dev) return -1;
    
    uint8_t cdb[6] = {0};
    cdb[0] = 0x1E;  /* PREVENT/ALLOW MEDIUM REMOVAL */
    cdb[4] = lock ? 0x01 : 0x00;
    
    return scsi_execute_sync(sr->scsi_dev, cdb, 6, NULL, 0, 0, 10000);
}

/*
 * ============================================================
 * Block Device Callbacks
 * ============================================================
 */

static int sr_read(blkdev_t *dev, uint64_t sector, uint32_t count, void *buffer) {
    sr_device_t *sr = (sr_device_t *)dev->priv;
    if (!sr || !sr->scsi_dev) return -1;
    
    scsi_device_t *scsi = sr->scsi_dev;
    uint8_t cdb[10];
    
    /* Build READ(10) CDB */
    scsi_cdb_read_10(cdb, (uint32_t)sector, (uint16_t)count);
    
    int ret = scsi_execute_sync(scsi, cdb, 10, buffer,
                                 count * dev->sector_size,
                                 SCSI_REQ_READ, 60000);  /* CD-ROM can be slow */
    
    return (ret >= 0) ? (int)count : -1;
}

static int sr_write(blkdev_t *dev, uint64_t sector, uint32_t count, const void *buffer) {
    (void)dev;
    (void)sector;
    (void)count;
    (void)buffer;
    /* CD-ROMs are read-only for normal data CDs */
    return -1;
}

/*
 * ============================================================
 * SR Driver API
 * ============================================================
 */

/*
 * Attach a SCSI CD-ROM device
 */
int sr_attach(scsi_device_t *scsi_dev) {
    if (!scsi_dev || scsi_dev->type != SCSI_TYPE_CDROM) {
        return -1;
    }
    
    /* Allocate sr_device */
    static sr_device_t sr_pool[8];
    if (sr_count >= 8) {
        kprintf("sr: too many CD-ROM drives\n");
        return -1;
    }
    
    sr_device_t *sr = &sr_pool[sr_count];
    memset(sr, 0, sizeof(*sr));
    
    sr->scsi_dev = scsi_dev;
    sr->drive_num = sr_count;
    sr->media_present = scsi_dev->media_present;
    
    /* Initialize block device */
    snprintf(sr->blkdev.name, sizeof(sr->blkdev.name), "sr%u", sr_count);
    sr->blkdev.sector_size = CD_SECTOR_SIZE;  /* Default to 2048 */
    sr->blkdev.total_sectors = scsi_dev->capacity;
    sr->blkdev.priv = sr;
    sr->blkdev.read = sr_read;
    sr->blkdev.write = sr_write;  /* Returns error */
    
    /* Register with block device subsystem */
    blkdev_register(&sr->blkdev);
    
    /* Add to list */
    sr->next = sr_list;
    sr_list = sr;
    sr_count++;
    
    kprintf("sr: attached %s [%s %s] %s\n",
            sr->blkdev.name,
            scsi_dev->vendor,
            scsi_dev->product,
            sr->media_present ? "media present" : "no media");
    
    return 0;
}

/*
 * Detach a SCSI CD-ROM device
 */
int sr_detach(scsi_device_t *scsi_dev) {
    sr_device_t **pp = &sr_list;
    
    while (*pp) {
        if ((*pp)->scsi_dev == scsi_dev) {
            sr_device_t *sr = *pp;
            *pp = sr->next;
            
            kprintf("sr: detached %s\n", sr->blkdev.name);
            return 0;
        }
        pp = &(*pp)->next;
    }
    
    return -1;
}

/*
 * Get SR device by name
 */
sr_device_t *sr_lookup(const char *name) {
    for (sr_device_t *sr = sr_list; sr; sr = sr->next) {
        if (strcmp(sr->blkdev.name, name) == 0) {
            return sr;
        }
    }
    return NULL;
}

/*
 * Initialize sr driver
 */
void sr_init(void) {
    kprintf("sr: SCSI CD-ROM driver initialized\n");
}
