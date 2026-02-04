#ifndef _MINIX_H
#define _MINIX_H

#include <stdint.h>
#include <vfs/vfs.h>

/* Minix V1/V2 constants */
#define MINIX_ROOT_INODE 1
#define MINIX_BLOCK_SIZE 1024

/* Magic numbers */
#define MINIX_V1_Magic      0x137F  /* 30 char names */
#define MINIX_V1_Magic_14   0x138F  /* 14 char names */
#define MINIX_V2_Magic      0x2468  /* 30 char names */
#define MINIX_V2_Magic_14   0x2478  /* 14 char names */
#define MINIX_V3_Magic      0x4D5A

/* Superblock structure */
struct minix_superblock {
    uint16_t s_ninodes;       /* Number of inodes */
    uint16_t s_nzones;        /* Number of zones (V1) */
    uint16_t s_imap_blocks;   /* Number of inode map blocks */
    uint16_t s_zmap_blocks;   /* Number of zone map blocks */
    uint16_t s_firstdatazone; /* First data zone */
    uint16_t s_log_zone_size; /* Log2 of zone size */
    uint32_t s_max_size;      /* Max file size */
    uint16_t s_magic;         /* Magic number */
    uint16_t s_state;         /* Filesystem state (V2 only) */
    uint32_t s_zones;         /* Number of zones (V2) */
} __attribute__((packed));

/* On-disk Inode structure (V1) */
struct minix_inode_v1 {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_time;
    uint8_t  i_gid;
    uint8_t  i_nlinks;
    uint16_t i_zone[9];
} __attribute__((packed));

/* On-disk Inode structure (V2) */
struct minix_inode_v2 {
    uint16_t i_mode;
    uint16_t i_nlinks;
    uint16_t i_uid;
    uint16_t i_gid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_mtime;
    uint32_t i_ctime;
    uint32_t i_zone[10];
} __attribute__((packed));

/* Directory Entry (V1) */
struct minix_dirent_v1 {
    uint16_t inode;
    char name[30];
} __attribute__((packed));

/* Internal Minix FS node */
typedef struct {  
    struct minix_superblock sb;
    fs_node_t *block_device;
    // Add other internal state if needed
    uint32_t last_inode_alloc;
    uint32_t last_zone_alloc;
} minix_fs_t;

void minix_init(void);

#endif
