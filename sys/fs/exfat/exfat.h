#ifndef _EXFAT_H
#define _EXFAT_H

#include <stdint.h>
#include <sys/lock.h>
#include <vfs/vfs.h>

/*
 * exFAT (Microsoft Extensible File Allocation Table) driver.
 *
 * Read-write driver: mounts an exFAT volume and supports directory
 * traversal (readdir/finddir), file read/write, truncate, create
 * (mkdir/mknod/O_CREAT), unlink, rmdir, rename, statfs and volume-label
 * probing.  Writes maintain the allocation bitmap, the FAT cluster chains,
 * the directory entry-set SetChecksum and NameHash, and the up-case table
 * is loaded at mount time and used for both name comparison and NameHash.
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

/* FAT-entry sentinels (32-bit entries).  Valid next-cluster indices are
 * 2..ClusterCount+1 (ClusterCount is capped at 0xFFFFFFF5), so any entry >=
 * EXFAT_CLUSTER_BAD is a non-continuation: the BAD marker (0xFFFFFFF7), the
 * reserved range, or the end-of-chain marker (0xFFFFFFFF).  audit L3: chain
 * walks stop at EXFAT_CLUSTER_END so a 0xFFFFFFF7 BAD entry is never followed. */
#define EXFAT_FIRST_CLUSTER    2U
#define EXFAT_CLUSTER_BAD      0xFFFFFFF7U  /* "bad cluster" FAT marker */
#define EXFAT_CLUSTER_END      0xFFFFFFF7U  /* stop-walk threshold (>= is end/bad) */
#define EXFAT_CLUSTER_EOF      0xFFFFFFFFU  /* end-of-chain marker we write */

/* VolumeFlags bits (Main Boot Sector). */
#define EXFAT_VOLFLAG_ACTIVE_FAT  0x0001
#define EXFAT_VOLFLAG_DIRTY       0x0002
#define EXFAT_VOLFLAG_MEDIA_FAIL  0x0004

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

    /* Write-side metadata, loaded at mount time. */
    uint8_t  *bitmap;                  /* allocation bitmap, in-memory copy */
    uint32_t  bitmap_bytes;            /* bitmap size in bytes */
    uint32_t  bitmap_cluster;          /* first cluster of the bitmap chain */
    uint8_t   bitmap_no_fat_chain;     /* bitmap uses contiguous clusters */
    uint16_t *upcase;                  /* 65536-entry BMP up-case fold table */
    uint32_t  free_clusters;           /* cached clear-bit count (statfs) */

    /* exFAT-audit H3: serialises every metadata mutation on this mount
     * (allocate/free clusters, FAT-chain edits, allocation-bitmap RMW,
     * directory-entry-set create/update/delete, node size updates).  Held at
     * the top-level VFS mutation op; the helpers it calls never take it. */
    mutex_t  lock;
} exfat_fs_t;

/* Per-node context (attached to fs_node.impl). */
typedef struct exfat_node {
    exfat_fs_t *fs;
    uint32_t first_cluster;
    uint64_t size;
    uint16_t attr;
    uint8_t  no_fat_chain;             /* clusters are contiguous */

    /* Location of this node's own directory entry set within its parent,
     * so writes can update the on-disk stream entry (DataLength, cluster).
     * has_dir_entry is 0 for the synthetic root (no parent entry). */
    uint8_t  has_dir_entry;
    uint32_t dir_cluster;              /* parent directory first cluster */
    uint8_t  dir_no_fat_chain;         /* parent directory chain type */
    uint64_t primary_index;            /* 0x85 entry index within the parent */
    uint8_t  secondary_count;          /* # of secondary entries in the set */

    struct dirent current_dirent;      /* per-node readdir scratch */
    /* exFAT-F3: live-open count; a pinned slot is never recycled.  Same
     * hazard the root-node comment in exfat_alloc_node() describes, but for
     * every other slot: sys_open puts this pointer straight into f->f_data,
     * so recycling a slot redirects an existing fd to a different file. */
    uint32_t pin;
} exfat_node_t;

#define EXFAT_NODE_CACHE_SIZE 128

void exfat_init(void);
struct blkdev;
/* Read the exFAT volume label from a raw block device (root-dir 0x83 entry). */
int exfat_read_label(struct blkdev *dev, char *label, size_t len);

#endif
