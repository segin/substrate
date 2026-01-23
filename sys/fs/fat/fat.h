#ifndef _FAT_H
#define _FAT_H

#include <stdint.h>
#include <vfs/vfs.h>

// FAT attribute flags
#define FAT_ATTR_READ_ONLY 0x01
#define FAT_ATTR_HIDDEN    0x02
#define FAT_ATTR_SYSTEM    0x04
#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_ARCHIVE   0x20
#define FAT_ATTR_LFN       0x0F

// BIOS Parameter Block (common to all FAT variants)
typedef struct {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint16_t root_entries;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
} __attribute__((packed)) fat_bpb_t;

// FAT32 Extended Boot Record
typedef struct {
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot;
    uint8_t  reserved[12];
    uint8_t  drive_num;
    uint8_t  reserved1;
    uint8_t  boot_sig;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
} __attribute__((packed)) fat32_ext_bpb_t;

// Directory Entry (8.3 format)
typedef struct {
    char     name[11];
    uint8_t  attr;
    uint8_t  nt_res;
    uint8_t  crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_acc_date;
    uint16_t cluster_high;
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t cluster_low;
    uint32_t file_size;
} __attribute__((packed)) fat_dirent_t;

// Long Filename Entry
typedef struct {
    uint8_t  order;
    uint16_t name1[5];
    uint8_t  attr;
    uint8_t  type;
    uint8_t  checksum;
    uint16_t name2[6];
    uint16_t cluster;
    uint16_t name3[2];
} __attribute__((packed)) fat_lfn_t;

// FAT Filesystem Context (per-mount)
typedef struct fat_fs {
    fs_node_t *device;              // Block device node
    fat_bpb_t bpb;                  // BIOS Parameter Block
    fat32_ext_bpb_t ext_bpb;        // FAT32 extended BPB
    uint32_t fat_type;              // 12, 16, or 32
    uint32_t fat_start_sector;      // First sector of FAT
    uint32_t root_dir_sectors;      // Root directory sectors (FAT12/16)
    uint32_t first_data_sector;     // First data cluster sector
    uint32_t total_clusters;        // Total data clusters
    uint32_t cluster_size;          // Bytes per cluster
    uint8_t *fat_table;             // Cached FAT table
    uint32_t fat_table_size;        // Size of cached FAT in bytes
} fat_fs_t;

// FAT File/Directory Node Context
typedef struct fat_node {
    fat_fs_t *fs;                   // Filesystem context
    uint32_t first_cluster;         // Starting cluster
    uint32_t size;                  // File size in bytes
    uint32_t current_cluster;       // Current cluster (for sequential access)
    uint32_t current_offset;        // Current offset within file
    uint8_t attr;                   // FAT attributes
} fat_node_t;

// Public functions
void fat_init(void);
int fat_parse_lfn(fat_lfn_t *lfn, char *buffer);

#endif
