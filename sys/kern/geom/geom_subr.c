/*
 * geom_subr.c - GEOM Core Subroutines
 *
 * Core functionality for disk and partition management:
 * - Class registration and priority ordering
 * - Disk registration and partition scanning
 * - Partition list management
 * - Sector I/O helpers
 */

#include "geom.h"
#include "../console.h"
#include "../../drivers/storage/blkdev.h"
#include <string.h>
#include <stdio.h>

extern void *kmalloc(size_t size);
extern void kfree(void *ptr);

/*
 * ============================================================
 * Global State
 * ============================================================
 */

/* Registered partition table classes (priority-sorted linked list) */
static geom_class_t *geom_classes = NULL;

/* Registered disks */
static geom_disk_t *geom_disks = NULL;

/* Global partition list for lookup */
static geom_partition_t *geom_all_partitions = NULL;

/*
 * ============================================================
 * Well-Known GPT Type GUIDs
 * ============================================================
 */

/* EFI System Partition: C12A7328-F81F-11D2-BA4B-00A0C93EC93B */
const uint8_t GEOM_GPT_TYPE_EFI_SYSTEM[16] = {
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
};

/* Microsoft Basic Data: EBD0A0A2-B9E5-4433-87C0-68B6B72699C7 */
const uint8_t GEOM_GPT_TYPE_MS_BASIC_DATA[16] = {
    0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
    0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
};

/* Linux Filesystem: 0FC63DAF-8483-4772-8E79-3D69D8477DE4 */
const uint8_t GEOM_GPT_TYPE_LINUX_FS[16] = {
    0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
    0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4
};

/* Linux Swap: 0657FD6D-A4AB-43C4-84E5-0933C84B4F4F */
const uint8_t GEOM_GPT_TYPE_LINUX_SWAP[16] = {
    0x6D, 0xFD, 0x57, 0x06, 0xAB, 0xA4, 0xC4, 0x43,
    0x84, 0xE5, 0x09, 0x33, 0xC8, 0x4B, 0x4F, 0x4F
};

/* FreeBSD UFS: 516E7CB4-6ECF-11D6-8FF8-00022D09712B */
const uint8_t GEOM_GPT_TYPE_FREEBSD_UFS[16] = {
    0xB4, 0x7C, 0x6E, 0x51, 0xCF, 0x6E, 0xD6, 0x11,
    0x8F, 0xF8, 0x00, 0x02, 0x2D, 0x09, 0x71, 0x2B
};

/* FreeBSD ZFS: 516E7CBA-6ECF-11D6-8FF8-00022D09712B */
const uint8_t GEOM_GPT_TYPE_FREEBSD_ZFS[16] = {
    0xBA, 0x7C, 0x6E, 0x51, 0xCF, 0x6E, 0xD6, 0x11,
    0x8F, 0xF8, 0x00, 0x02, 0x2D, 0x09, 0x71, 0x2B
};

/*
 * ============================================================
 * Initialization
 * ============================================================
 */

void geom_init(void) {
    geom_classes = NULL;
    geom_disks = NULL;
    geom_all_partitions = NULL;
    
    /* Classes are registered by their own init functions */
}

/*
 * ============================================================
 * Class Registration (Priority-Sorted)
 * ============================================================
 */

void geom_register_class(geom_class_t *cls) {
    if (!cls) return;
    
    /* Insert in priority order (higher priority first) */
    geom_class_t **pp = &geom_classes;
    while (*pp && (*pp)->priority >= cls->priority) {
        pp = &(*pp)->next;
    }
    cls->next = *pp;
    *pp = cls;
}

/*
 * ============================================================
 * Sector I/O Helpers
 * ============================================================
 */

int geom_read_sector(geom_disk_t *disk, uint64_t lba, void *buf) {
    if (!disk || !disk->read || !buf) return -1;
    return disk->read(disk, lba, 1, buf);
}

int geom_read_sectors(geom_disk_t *disk, uint64_t lba, size_t count, void *buf) {
    if (!disk || !disk->read || !buf || count == 0) return -1;
    return disk->read(disk, lba, count, buf);
}

/*
 * ============================================================
 * Partition Management
 * ============================================================
 */

/*
 * Partition Block Device Wrappers
 */

static int geom_part_read(blkdev_t *dev, uint64_t sector, uint32_t count, void *buffer) {
    geom_partition_t *part = (geom_partition_t *)dev->priv;
    if (!part || !part->disk) return -1;
    
    /* Bounds check */
    if (sector + count > part->size_sectors) return -1;
    
    /* Delegate to parent disk with offset */
    return part->disk->read(part->disk, part->start_lba + sector, count, buffer);
}

static int geom_part_write(blkdev_t *dev, uint64_t sector, uint32_t count, const void *buffer) {
    geom_partition_t *part = (geom_partition_t *)dev->priv;
    if (!part || !part->disk) return -1;
    
    /* Bounds check */
    if (sector + count > part->size_sectors) return -1;
    
    /* Delegate to parent disk with offset */
    if (part->disk->write) {
        return part->disk->write(part->disk, part->start_lba + sector, count, buffer);
    }
    return -1;
}

geom_partition_t *geom_add_partition(geom_disk_t *disk, const char *name,
                                      uint64_t start, uint64_t size,
                                      uint8_t mbr_type, uint8_t bsd_fstype,
                                      const uint8_t *guid, const char *type_name,
                                      uint32_t flags) {
    if (!disk || !name || size == 0) return NULL;
    
    /* Allocate partition structure */
    geom_partition_t *part = kmalloc(sizeof(geom_partition_t));
    if (!part) return NULL;
    
    memset(part, 0, sizeof(*part));
    
    /* Fill in fields */
    strncpy(part->name, name, sizeof(part->name) - 1);
    part->disk = disk;
    part->start_lba = start;
    part->size_sectors = size;
    part->mbr_type = mbr_type;
    part->bsd_fstype = bsd_fstype;
    part->flags = flags;
    
    if (guid) {
        memcpy(part->guid, guid, 16);
    }
    
    if (type_name) {
        strncpy(part->type_name, type_name, sizeof(part->type_name) - 1);
    }
    
    /* Add to disk's partition list */
    part->next = disk->partitions;
    disk->partitions = part;
    disk->partition_count++;
    
    /* Add to global partition list */
    geom_partition_t *global_copy = kmalloc(sizeof(geom_partition_t));
    if (global_copy) {
        memcpy(global_copy, part, sizeof(*part));
        global_copy->next = geom_all_partitions;
        geom_all_partitions = global_copy;
    }
    
    /* Register as block device in /dev/storage/ */
    blkdev_t *bdev = kmalloc(sizeof(blkdev_t));
    if (bdev) {
        memset(bdev, 0, sizeof(*bdev));
        strncpy(bdev->name, name, sizeof(bdev->name) - 1);
        bdev->sector_size = disk->sector_size;
        bdev->total_sectors = size;
        bdev->priv = part;
        bdev->read = geom_part_read;
        bdev->write = geom_part_write;
        
        blkdev_register(bdev);
    }
    
    return part;
}

geom_partition_t *geom_find_partition(const char *name) {
    if (!name) return NULL;
    
    for (geom_partition_t *p = geom_all_partitions; p; p = p->next) {
        if (strcmp(p->name, name) == 0) {
            return p;
        }
    }
    return NULL;
}

int geom_get_partition_count(geom_disk_t *disk) {
    return disk ? disk->partition_count : 0;
}

/*
 * ============================================================
 * Partition Scanning
 * ============================================================
 */

void geom_scan(geom_disk_t *disk, uint64_t offset, int depth, const char *prefix) {
    if (!disk) return;
    
    /* Recursion limit */
    if (depth > GEOM_MAX_RECURSION) {
        kprint("GEOM: maximum recursion depth exceeded\n");
        return;
    }
    
    /* Try each registered class in priority order */
    for (geom_class_t *cls = geom_classes; cls; cls = cls->next) {
        if (cls->sniff && cls->sniff(disk, offset, depth, prefix) == 0) {
            /* Found a matching partition table */
            return;
        }
    }
    
    /* No partition table found at this offset */
    if (depth == 0) {
        kprint("  ");
        kprint(disk->name);
        kprint(": no partition table detected\n");
    }
}

/*
 * ============================================================
 * Disk Registration
 * ============================================================
 */

void geom_register_disk(geom_disk_t *disk) {
    if (!disk) return;
    
    /* Initialize partition list */
    disk->partitions = NULL;
    disk->partition_count = 0;
    
    /* Add to disk list */
    disk->next = geom_disks;
    geom_disks = disk;
    
    /* Scan for partition tables starting at LBA 0 */
    geom_scan(disk, 0, 0, disk->name);
}

/*
 * ============================================================
 * Type Name Helpers
 * ============================================================
 */

const char *geom_mbr_type_name(uint8_t type) {
    switch (type) {
    case GEOM_MBR_EMPTY:        return "Empty";
    case GEOM_MBR_FAT12:        return "FAT12";
    case GEOM_MBR_FAT16_SMALL:  return "FAT16 <32M";
    case GEOM_MBR_EXTENDED:     return "Extended";
    case GEOM_MBR_FAT16:        return "FAT16";
    case GEOM_MBR_NTFS:         return "NTFS";
    case GEOM_MBR_FAT32:        return "FAT32";
    case GEOM_MBR_FAT32_LBA:    return "FAT32 LBA";
    case GEOM_MBR_FAT16_LBA:    return "FAT16 LBA";
    case GEOM_MBR_EXTENDED_LBA: return "Extended LBA";
    case GEOM_MBR_LINUX_SWAP:   return "Linux swap";
    case GEOM_MBR_LINUX:        return "Linux";
    case GEOM_MBR_LINUX_LVM:    return "Linux LVM";
    case GEOM_MBR_FREEBSD:      return "FreeBSD";
    case GEOM_MBR_OPENBSD:      return "OpenBSD";
    case GEOM_MBR_NETBSD:       return "NetBSD";
    case GEOM_MBR_GPT_PROTECTIVE: return "GPT Protective";
    case GEOM_MBR_EFI_SYSTEM:   return "EFI System";
    default:                    return "Unknown";
    }
}

const char *geom_bsd_fstype_name(uint8_t fstype) {
    switch (fstype) {
    case GEOM_BSD_FS_UNUSED:    return "unused";
    case GEOM_BSD_FS_SWAP:      return "swap";
    case GEOM_BSD_FS_BSDFFS:    return "4.2BSD";
    case GEOM_BSD_FS_MSDOS:     return "MSDOS";
    case GEOM_BSD_FS_BSDLFS:    return "4.4LFS";
    case GEOM_BSD_FS_OTHER:     return "other";
    case GEOM_BSD_FS_HPFS:      return "HPFS";
    case GEOM_BSD_FS_ISO9660:   return "ISO9660";
    case GEOM_BSD_FS_BOOT:      return "boot";
    case GEOM_BSD_FS_VINUM:     return "Vinum";
    case GEOM_BSD_FS_RAID:      return "RAID";
    case GEOM_BSD_FS_ZFS:       return "ZFS";
    default:                    return "unknown";
    }
}

/*
 * Compare GPT type GUIDs
 */
int geom_guid_equal(const uint8_t *a, const uint8_t *b) {
    return memcmp(a, b, 16) == 0;
}

/*
 * Check if GUID is all zeros (unused entry)
 */
int geom_guid_is_zero(const uint8_t *guid) {
    for (int i = 0; i < 16; i++) {
        if (guid[i] != 0) return 0;
    }
    return 1;
}
