#ifndef _EXT2_H
#define _EXT2_H

#include <stdint.h>
#include <stddef.h>
#include <vfs/vfs.h>

// EXT2 Magic Number
#define EXT2_SUPER_MAGIC 0xEF53

// File Types (for directory entries)
#define EXT2_FT_UNKNOWN  0
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR      2
#define EXT2_FT_CHRDEV   3
#define EXT2_FT_BLKDEV   4
#define EXT2_FT_FIFO     5
#define EXT2_FT_SOCK     6
#define EXT2_FT_SYMLINK  7

// Inode Types (i_mode)
#define EXT2_S_IFSOCK  0xC000
#define EXT2_S_IFLNK   0xA000
#define EXT2_S_IFREG   0x8000
#define EXT2_S_IFBLK   0x6000
#define EXT2_S_IFDIR   0x4000
#define EXT2_S_IFCHR   0x2000
#define EXT2_S_IFIFO   0x1000

// Special inodes
#define EXT2_ROOT_INO      2
#define EXT2_GOOD_OLD_INODE_SIZE 128

// Superblock (at offset 1024)
typedef struct {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    // Extended superblock fields (rev >= 1)
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algo_bitmap;
} __attribute__((packed)) ext2_superblock_t;

// Block Group Descriptor
typedef struct {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint32_t bg_reserved[3];
} __attribute__((packed)) ext2_group_desc_t;

// Inode structure
typedef struct {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15];  // 0-11: direct, 12: indirect, 13: double indirect, 14: triple indirect
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t  i_osd2[12];
} __attribute__((packed)) ext2_inode_t;

// Directory Entry
typedef struct {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[];
} __attribute__((packed)) ext2_dirent_t;

// EXT2 filesystem context (per-mount)
typedef struct {
    fs_node_t *device;          // Block device node
    ext2_superblock_t sb;       // Superblock
    ext2_group_desc_t *bgd;     // Block group descriptor table
    uint32_t block_size;        // Block size in bytes
    uint32_t inodes_per_group;
    uint32_t blocks_per_group;
    uint32_t group_count;
    uint32_t inode_size;
    // Hints for Next Fit allocation
    uint32_t last_alloc_group;
    uint32_t last_alloc_bit;
} ext2_fs_t;

// EXT2 file/directory node context
typedef struct {
    ext2_fs_t *fs;
    uint32_t inode_num;
    ext2_inode_t inode;
} ext2_node_t;

// Public functions
void ext2_init(void);

// Internal helpers (exposed for testing)
int ext2_read_inode(ext2_fs_t *fs, uint32_t inode_num, ext2_inode_t *inode);
uint32_t ext2_read_block(ext2_fs_t *fs, uint32_t block_num, void *buffer);
uint32_t ext2_inode_read(ext2_fs_t *fs, ext2_inode_t *inode, off_t offset, uint32_t size, void *buffer);

#endif
