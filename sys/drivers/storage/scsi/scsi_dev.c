/*
 * scsi_dev.c - Unified SCSI Device Driver
 *
 * High-level driver for all SCSI device types.
 * No artificial sd/sr split - just SCSI devices with type-specific handling.
 */

#include <stdio.h>
#include <string.h>

#include <drivers/storage/blkdev.h>
#include <drivers/storage/scsi/scsi.h>
#include <kern/console.h>

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
static uint32_t scsi_dev_next_num = 0;
static scsi_blk_dev_t scsi_dev_pool[32];

static scsi_blk_dev_t *scsi_dev_alloc_slot(void) {
    for (size_t i = 0; i < (sizeof(scsi_dev_pool) / sizeof(scsi_dev_pool[0])); i++) {
        if (scsi_dev_pool[i].scsi_dev == NULL) {
            return &scsi_dev_pool[i];
        }
    }
    return NULL;
}

/*
 * ============================================================
 * Device Type Constants
 * ============================================================
 */
#define CD_SECTOR_SIZE      2048    /* Standard data CD */

static int scsi_dev_refresh_capacity(scsi_device_t *scsi_dev) {
    uint64_t sectors = 0;
    uint32_t sector_size = 0;

    if (scsi_dev == NULL) {
        return -1;
    }

    if (scsi_read_capacity(scsi_dev, &sectors, &sector_size) < 0) {
        return -1;
    }

    /*
    kprintf("scsi: %d:%d:%d capacity %u sectors, sector_size %u\n",
            scsi_dev->bus, scsi_dev->target, scsi_dev->lun,
            (uint32_t)sectors, sector_size);
    */

    scsi_dev->capacity = sectors;
    scsi_dev->sector_size = sector_size;
    return 0;
}

/*
 * ============================================================
 * Block Device Callbacks
 * ============================================================
 */

static int scsi_blk_read(blkdev_t *dev, uint64_t sector, uint32_t count, void *buffer) {
    scsi_blk_dev_t *sbd = (scsi_blk_dev_t *)dev->priv;
    if (!sbd || !sbd->scsi_dev) return -1;
    
    scsi_device_t *scsi = sbd->scsi_dev;
    int ret;
    int refreshed = 0;

retry:
    if (sector > 0xFFFFFFFFULL || count > 0xFFFFU || scsi->capacity > 0x100000000ULL) {
        uint8_t cdb[16];

        scsi_cdb_read_16(cdb, sector, count);
        ret = scsi_execute_sync(scsi, cdb, 16, buffer,
                                count * dev->sector_size,
                                SCSI_REQ_READ, 60000);
    } else {
        uint8_t cdb[10];

        scsi_cdb_read_10(cdb, (uint32_t)sector, (uint16_t)count);
        ret = scsi_execute_sync(scsi, cdb, 10, buffer,
                                count * dev->sector_size,
                                SCSI_REQ_READ, 60000);
    }
    
    if (ret < 0 && !refreshed &&
        scsi->type == SCSI_TYPE_ROM && scsi->removable) {
        scsi->flags &= ~SCSI_DEV_ONLINE;
        if (scsi_dev_refresh_capacity(scsi) == 0) {
            refreshed = 1;
            scsi->flags |= SCSI_DEV_ONLINE;
            scsi->online = 1;
            scsi->media_present = 1;
            scsi->removable = 1;
            /*
             * SCSI-05: push the NEW medium's geometry into the block device.
             * The refresh updated scsi->capacity and scsi->sector_size but
             * stopped there, so blkdev kept the size of the disc that had
             * just been ejected -- every subsequent read was bounded by the
             * old capacity and the partition table cached from the old disc
             * stayed live.  Insert a smaller disc and reads ran off its end;
             * insert a larger one and most of it was unreachable.
             */
            dev->sector_size   = scsi->sector_size ? scsi->sector_size
                                                   : dev->sector_size;
            dev->total_sectors = scsi->capacity;
            goto retry;
        }
    }
    return (ret >= 0) ? 0 : -1;
}

static int scsi_blk_write(blkdev_t *dev, uint64_t sector, uint32_t count, const void *buffer) {
    scsi_blk_dev_t *sbd = (scsi_blk_dev_t *)dev->priv;
    if (!sbd || !sbd->scsi_dev) return -1;
    
    scsi_device_t *scsi = sbd->scsi_dev;
    
    /* CD-ROMs and WORM media are not writable through the generic block path. */
    if (scsi->type == SCSI_TYPE_ROM ||
        scsi->type == SCSI_TYPE_WORM ||
        scsi->write_protected) {
        return -1;
    }
    
    int ret;

    if (sector > 0xFFFFFFFFULL || count > 0xFFFFU || scsi->capacity > 0x100000000ULL) {
        uint8_t cdb[16];

        scsi_cdb_write_16(cdb, sector, count);
        ret = scsi_execute_sync(scsi, cdb, 16, (void *)buffer,
                                count * dev->sector_size,
                                SCSI_REQ_WRITE, 30000);
    } else {
        uint8_t cdb[10];

        scsi_cdb_write_10(cdb, (uint32_t)sector, (uint16_t)count);
        ret = scsi_execute_sync(scsi, cdb, 10, (void *)buffer,
                                count * dev->sector_size,
                                SCSI_REQ_WRITE, 30000);
    }

    /* blkdev_do_write() treats any nonzero return as an error, so report 0 on
     * success (matching scsi_blk_read) -- returning the sector count made every
     * successful write look failed, invalidating the cache and looping. */
    return (ret >= 0) ? 0 : -1;
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
    
    int ret = scsi_execute_sync(dev, cdb, 10, buffer, buflen,
                                SCSI_REQ_READ, 30000);

    if (ret < 0 && dev->type == SCSI_TYPE_ROM && dev->removable) {
        dev->flags &= ~SCSI_DEV_ONLINE;
        if (scsi_dev_refresh_capacity(dev) == 0) {
            dev->flags |= SCSI_DEV_ONLINE;
            dev->online = 1;
            dev->media_present = 1;
            dev->removable = 1;
            ret = scsi_execute_sync(dev, cdb, 10, buffer, buflen,
                                    SCSI_REQ_READ, 30000);
        }
    }

    return ret;
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
    
    scsi_blk_dev_t *sbd = scsi_dev_alloc_slot();
    if (!sbd) {
        kprint("scsi: no free block-device slots\n");
        return -1;
    }
    memset(sbd, 0, sizeof(*sbd));
    
    sbd->scsi_dev = scsi_dev;
    sbd->dev_num = scsi_dev_next_num++;

    if ((scsi_dev->type == SCSI_TYPE_DISK ||
         scsi_dev->type == SCSI_TYPE_ROM ||
         scsi_dev->type == SCSI_TYPE_OPTICAL ||
         scsi_dev->type == SCSI_TYPE_WORM) &&
        (scsi_dev->capacity == 0 || scsi_dev->sector_size == 0)) {
        (void)scsi_dev_refresh_capacity(scsi_dev);
    }
    
    /* Create device name: scsi0, scsi1, etc. */
    snprintf(sbd->blkdev.name, sizeof(sbd->blkdev.name), "scsi%u", sbd->dev_num);
    
    /*
     * SCSI-04: reconcile the block layer's sector size with what the DEVICE
     * reported, instead of hardcoding 2048 for every optical unit.
     *
     * total_sectors comes from READ CAPACITY and is counted in the device's
     * own blocks, so pairing it with a sector_size the device did not report
     * describes a disk that does not exist: with a real 512-byte optical
     * device the geometry came out 4x wrong, and with a 2048-byte one that
     * reported its size correctly the pairing happened to work only by
     * coincidence.  Trust the reported size when there is one -- 2048 stays
     * the fallback for optical media that answers READ CAPACITY with
     * nothing useful, which is the case the constant was there for.
     */
    if (scsi_dev->sector_size) {
        sbd->blkdev.sector_size = scsi_dev->sector_size;
    } else if (scsi_dev->type == SCSI_TYPE_ROM ||
               scsi_dev->type == SCSI_TYPE_OPTICAL ||
               scsi_dev->type == SCSI_TYPE_WORM) {
        sbd->blkdev.sector_size = CD_SECTOR_SIZE;
    } else {
        sbd->blkdev.sector_size = 512;
    }

    sbd->blkdev.total_sectors = scsi_dev->capacity;
    sbd->blkdev.priv = sbd;
    sbd->blkdev.read = scsi_blk_read;
    sbd->blkdev.write = scsi_blk_write;
    
    /*
     * Register the raw node, and only hand the device to GEOM for a partition
     * scan when scanning it actually makes sense.
     *
     * Two cases are skipped:
     *
     * 1. OPTICAL MEDIA (ROM / OPTICAL / WORM).  Never scanned, regardless of
     *    whether a disc is present.  Optical media carries no MBR/GPT/BSD
     *    label -- it is ISO9660 -- so the scan is pointless, and it is
     *    actively hazardous: these devices use 2048-byte sectors, and a
     *    2048-byte sector read overran the sniffers' 512-byte stack buffers
     *    before they were bounded (the CD-ROM boot crash).  The IDE probe has
     *    always done this for ATAPI -- see the matching comment in
     *    ide_probe.c -- and the SCSI attach path simply never got the same
     *    rule, so every SATAPI/USB/ATAPI-over-SCSI drive was being sniffed.
     *
     * 2. NO MEDIA (capacity still zero).  scsi_dev_refresh_capacity() above
     *    has already had its retry, so a zero capacity means the slot is
     *    genuinely empty.  Sniffing it issues reads that cannot succeed:
     *    every one fails, each is retried, and on a USB Bulk-Only reader the
     *    resulting timeout drives a Mass Storage Reset of the whole device,
     *    disturbing the other LUNs -- including a slot with a working card.
     *
     * The /dev/storage/scsiN node is created either way, so nothing that
     * expects the device to exist breaks; only the scan is skipped, and the
     * reason is logged rather than being silent.
     */
    {
        int optical = (scsi_dev->type == SCSI_TYPE_ROM ||
                       scsi_dev->type == SCSI_TYPE_OPTICAL ||
                       scsi_dev->type == SCSI_TYPE_WORM);
        const char *skip = NULL;

        if (optical) {
            skip = "optical media, partition scan skipped";
        } else if (sbd->blkdev.total_sectors == 0) {
            skip = "no media, partition scan skipped";
        }

        if (skip) {
            blkdev_register(&sbd->blkdev);
            char nm_buf[96];
            snprintf(nm_buf, sizeof(nm_buf), "  %s: %s\n",
                     sbd->blkdev.name, skip);
            kprint(nm_buf);
        } else {
            blkdev_register_disk(&sbd->blkdev);
        }
    }
    
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
    snprintf(log_buf, sizeof(log_buf), "scsi: attached %s (%s) [%s %s]\n",
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

            if (scsi_dev->type == SCSI_TYPE_DISK ||
                scsi_dev->type == SCSI_TYPE_OPTICAL ||
                scsi_dev->type == SCSI_TYPE_WORM) {
                (void)scsi_synchronize_cache(scsi_dev);
            }

            blkdev_unregister(&sbd->blkdev);
            if (scsi_dev_count > 0) scsi_dev_count--;
            
            char log_buf[64];
            snprintf(log_buf, sizeof(log_buf), "scsi: detached %s\n", sbd->blkdev.name);
            kprint(log_buf);
            memset(sbd, 0, sizeof(*sbd));
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
    scsi_dev_list = NULL;
    scsi_dev_count = 0;
    scsi_dev_next_num = 0;
    memset(scsi_dev_pool, 0, sizeof(scsi_dev_pool));
    kprint("scsi: unified SCSI device driver initialized\n");
}
