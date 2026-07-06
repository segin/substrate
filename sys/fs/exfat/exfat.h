#ifndef _EXFAT_H
#define _EXFAT_H

#include <stdint.h>
#include <vfs/vfs.h>

/*
 * exFAT (Microsoft Extensible File Allocation Table) driver.
 *
 * This is a read-only driver: it mounts an exFAT volume and supports
 * directory traversal (readdir/finddir), file read, statfs and volume-label
 * probing.  Write support (allocation bitmap, up-case table, set checksums,
 * FAT-chain maintenance) is not implemented; the mount carries no write ops,
 * so the VFS rejects writes.
 */

/* On-disk FileAttributes (little-endian u16 in the file directory entry). */
#define EXFAT_ATTR_READ_ONLY   0x0001
#define EXFAT_ATTR_HIDDEN      0x0002
#define EXFAT_ATTR_SYSTEM      0x0004
#define EXFAT_ATTR_DIRECTORY   0x0010
#define EXFAT_ATTR_ARCHIVE     0x0020

/* Directory EntryType byte values.  Bit 0x80 ("in use") distinguishes a live
 * entry from a deleted/unused one (e.g. 0x05 is a deleted 0x85 entry). */
#define EXFAT_ENTRY_EOD        0x00  /* end of directory */
#define EXFAT_ENTRY_BITMAP     0x81  /* allocation bitmap */
#define EXFAT_ENTRY_UPCASE     0x82  /* up-case table */
#define EXFAT_ENTRY_LABEL      0x83  /* volume label */
#define EXFAT_ENTRY_FILE       0x85  /* file directory entry (primary) */
#define EXFAT_ENTRY_STREAM     0xC0  /* stream extension (secondary) */
#define EXFAT_ENTRY_NAME       0xC1  /* file name (secondary) */
#define EXFAT_ENTRY_INUSE      0x80  /* "in use" bit within EntryType */

/* GeneralSecondaryFlags (stream extension entry). */
#define EXFAT_FLAG_ALLOC_POSSIBLE 0x01
#define EXFAT_FLAG_NO_FAT_CHAIN   0x02  /* clusters are contiguous */

/* FAT-entry sentinels (32-bit entries). >= EXFAT_CLUSTER_END means end/bad. */
#define EXFAT_FIRST_CLUSTER    2U
#define EXFAT_CLUSTER_END      0xFFFFFFF8U

/* Main Boot Sector header (byte offsets < 512; only the header is needed). */
typedef struct {
    uint8_t  jump_boot[3];
    char     fs_name[8];               /* "EXFAT   " */
    uint8_t  must_be_zero[53];
    uint64_t partition_offset;         /* sectors */
    uint64_t volume_length;            /* total sectors */
    uint32_t fat_offset;               /* sectors */
    uint32_t fat_length;               /* sectors */
    uint32_t cluster_heap_offset;      /* sectors */
    uint32_t cluster_count;
    uint32_t root_cluster;
    uint32_t volume_serial;
    uint16_t fs_revision;
    uint16_t volume_flags;
    uint8_t  bytes_per_sector_shift;   /* log2(bytes/sector) */
    uint8_t  sectors_per_cluster_shift;/* log2(sectors/cluster) */
    uint8_t  num_fats;
    uint8_t  drive_select;
    uint8_t  percent_in_use;
    uint8_t  reserved[7];
} __attribute__((packed)) exfat_boot_t;

/* File Directory Entry (0x85) — the primary entry of a file's entry set. */
typedef struct {
    uint8_t  entry_type;
    uint8_t  secondary_count;          /* # of secondary entries following */
    uint16_t set_checksum;
    uint16_t file_attributes;
    uint16_t reserved1;
    uint32_t create_time;
    uint32_t modify_time;
    uint32_t access_time;
    uint8_t  create_10ms;
    uint8_t  modify_10ms;
    uint8_t  create_tz;
    uint8_t  modify_tz;
    uint8_t  access_tz;
    uint8_t  reserved2[7];
} __attribute__((packed)) exfat_file_entry_t;

/* Stream Extension Entry (0xC0). */
typedef struct {
    uint8_t  entry_type;
    uint8_t  flags;                    /* GeneralSecondaryFlags */
    uint8_t  reserved1;
    uint8_t  name_length;              /* # of UTF-16 chars in the name */
    uint16_t name_hash;
    uint16_t reserved2;
    uint64_t valid_data_length;
    uint32_t reserved3;
    uint32_t first_cluster;
    uint64_t data_length;
} __attribute__((packed)) exfat_stream_entry_t;

/* File Name Entry (0xC1) — 15 UTF-16LE code units of the name. */
typedef struct {
    uint8_t  entry_type;
    uint8_t  flags;
    uint16_t name[15];
} __attribute__((packed)) exfat_name_entry_t;

/* Volume Label Entry (0x83), found in the root directory. */
typedef struct {
    uint8_t  entry_type;
    uint8_t  char_count;               /* # of UTF-16 chars (0..11) */
    uint16_t label[11];
    uint8_t  reserved[8];
} __attribute__((packed)) exfat_label_entry_t;

/* Per-mount filesystem context. */
typedef struct exfat_fs {
    fs_node_t *device;                 /* backing block-device node */
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t cluster_size;             /* bytes */
    uint8_t  bytes_per_sector_shift;
    uint8_t  sectors_per_cluster_shift;
    uint64_t fat_offset;               /* sectors */
    uint64_t fat_length;               /* sectors */
    uint64_t cluster_heap_offset;      /* sectors */
    uint32_t cluster_count;
    uint32_t root_cluster;
    uint64_t volume_length;            /* sectors */
    fs_node_t *root_node;
} exfat_fs_t;

/* Per-node context (attached to fs_node.impl). */
typedef struct exfat_node {
    exfat_fs_t *fs;
    uint32_t first_cluster;
    uint64_t size;
    uint16_t attr;
    uint8_t  no_fat_chain;             /* clusters are contiguous */
    struct dirent current_dirent;      /* per-node readdir scratch */
} exfat_node_t;

#define EXFAT_NODE_CACHE_SIZE 64

void exfat_init(void);
struct blkdev;
/* Read the exFAT volume label from a raw block device (root-dir 0x83 entry). */
int exfat_read_label(struct blkdev *dev, char *label, size_t len);

#endif
