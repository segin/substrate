#ifndef _EXT2_H
#define _EXT2_H

#include <stdint.h>
#include <stddef.h>
#include <vfs/vfs.h>
#include <sys/lock.h>
#include <sys/stat.h>

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

/* Inode flags (i_flags) — substrate handles a subset.  The ext4
 * EXTENTS flag is the load-bearing one: when set, i_block[] is
 * NOT the legacy 12-direct/1-indir/2-indir/3-indir pointer array,
 * it's an inline ext4_extent_header followed by extents/indexes.
 * Reading the file requires walking the extent tree.  */
#define EXT4_EXTENTS_FL    0x00080000
/* This directory uses an htree index (root block holds index entries
 * instead of plain dirents past . and .. ).  ext4 default for any
 * directory that has grown past one block.  */
#define EXT2_INDEX_FL      0x00001000

/* htree hash variants (root block byte at h_hash_version).  Substrate
 * supports all six because mkfs.ext4 picks half_md4 by default and
 * older e2fsprogs picked legacy; we may meet either in the wild.  */
#define EXT2_HTREE_LEGACY            0
#define EXT2_HTREE_HALF_MD4          1
#define EXT2_HTREE_TEA               2
#define EXT2_HTREE_LEGACY_UNSIGNED   3
#define EXT2_HTREE_HALF_MD4_UNSIGNED 4
#define EXT2_HTREE_TEA_UNSIGNED      5
#define EXT2_HTREE_EOF               0x7FFFFFFF

/* Feature flag groups — sb.s_feature_{compat,incompat,ro_compat}.
 * Definitions taken verbatim from FreeBSD's ext2fs/ext2fs.h for
 * cross-reference clarity.  */
#define EXT2F_COMPAT_PREALLOC         0x0001
#define EXT2F_COMPAT_IMAGIC_INODES    0x0002
#define EXT2F_COMPAT_HASJOURNAL       0x0004   /* ext3+ */
#define EXT2F_COMPAT_EXT_ATTR         0x0008
#define EXT2F_COMPAT_RESIZE           0x0010
#define EXT2F_COMPAT_DIRHASHINDEX     0x0020   /* htree dir index */
#define EXT2F_COMPAT_LAZY_BG          0x0040
#define EXT2F_COMPAT_EXCLUDE_BITMAP   0x0100
#define EXT2F_COMPAT_SPARSESUPER2     0x0200

#define EXT2F_ROCOMPAT_SPARSESUPER    0x0001
#define EXT2F_ROCOMPAT_LARGEFILE      0x0002   /* >2GB files */
#define EXT2F_ROCOMPAT_BTREE_DIR      0x0004
#define EXT2F_ROCOMPAT_HUGE_FILE      0x0008
#define EXT2F_ROCOMPAT_GDT_CSUM       0x0010
#define EXT2F_ROCOMPAT_DIR_NLINK      0x0020
#define EXT2F_ROCOMPAT_EXTRA_ISIZE    0x0040   /* nsec timestamps */
#define EXT2F_ROCOMPAT_HAS_SNAPSHOT   0x0080
#define EXT2F_ROCOMPAT_QUOTA          0x0100
#define EXT2F_ROCOMPAT_BIGALLOC       0x0200
#define EXT2F_ROCOMPAT_METADATA_CKSUM 0x0400
#define EXT2F_ROCOMPAT_READONLY       0x1000
#define EXT2F_ROCOMPAT_PROJECT        0x2000

#define EXT2F_INCOMPAT_COMP           0x0001
#define EXT2F_INCOMPAT_FTYPE          0x0002   /* dirent file_type */
#define EXT2F_INCOMPAT_RECOVER        0x0004   /* needs journal replay */
#define EXT2F_INCOMPAT_JOURNAL_DEV    0x0008
#define EXT2F_INCOMPAT_META_BG        0x0010
#define EXT2F_INCOMPAT_EXTENTS        0x0040   /* ext4 extent tree */
#define EXT2F_INCOMPAT_64BIT          0x0080   /* >2^32 blocks */
#define EXT2F_INCOMPAT_MMP            0x0100
#define EXT2F_INCOMPAT_FLEX_BG        0x0200
#define EXT2F_INCOMPAT_CSUM_SEED      0x2000

/* The set we support.  Anything outside SUPP triggers a refuse-mount
 * (INCOMPAT) or ro-only mount (ROCOMPAT).  COMPAT bits never block
 * a mount, they're informational.  */
#define EXT2F_INCOMPAT_SUPP   (EXT2F_INCOMPAT_FTYPE | \
                               EXT2F_INCOMPAT_META_BG | \
                               EXT2F_INCOMPAT_EXTENTS | \
                               EXT2F_INCOMPAT_FLEX_BG | \
                               EXT2F_INCOMPAT_CSUM_SEED)
#define EXT2F_ROCOMPAT_SUPP   (EXT2F_ROCOMPAT_SPARSESUPER | \
                               EXT2F_ROCOMPAT_LARGEFILE | \
                               EXT2F_ROCOMPAT_DIR_NLINK | \
                               EXT2F_ROCOMPAT_HUGE_FILE | \
                               EXT2F_ROCOMPAT_EXTRA_ISIZE | \
                               EXT2F_ROCOMPAT_GDT_CSUM | \
                               EXT2F_ROCOMPAT_METADATA_CKSUM)

/* ext4 extent tree (when i_flags & EXT4_EXTENTS_FL).
 * Header sits at i_block[0..11] (60 bytes); contains up to 4
 * inline extents at depth 0, or up to 4 indexes pointing to
 * deeper levels.  Each level lives in one fs block — its own
 * header + array of leaf extents or indexes.  See FreeBSD's
 * fs/ext2fs/ext2_extents.h for the canonical reference.  */
#define EXT4_EXT_MAGIC          0xF30A
#define EXT4_EXT_DEPTH_MAX      5

typedef struct {
    uint16_t eh_magic;     /* 0xF30A */
    uint16_t eh_ecount;    /* valid entries in this node */
    uint16_t eh_max;       /* capacity of this node */
    uint16_t eh_depth;     /* 0 = leaf (extents); >0 = index */
    uint32_t eh_gen;
} __attribute__((packed)) ext4_extent_header_t;

/* Leaf-level entry: maps logical block range to physical range. */
typedef struct {
    uint32_t e_blk;        /* first logical block */
    uint16_t e_len;        /* number of blocks (initialised when
                              top bit is 0; uninitialised extent
                              for sparse-region preallocation when
                              top bit is set — we treat both the
                              same on read) */
    uint16_t e_start_hi;   /* high 16 bits of physical block */
    uint32_t e_start_lo;   /* low 32 bits */
} __attribute__((packed)) ext4_extent_t;

/* Index node entry: points to a child node holding more entries. */
typedef struct {
    uint32_t ei_blk;       /* first logical block covered */
    uint32_t ei_leaf_lo;   /* low 32 bits of child node phys block */
    uint16_t ei_leaf_hi;   /* high 16 bits */
    uint16_t ei_unused;
} __attribute__((packed)) ext4_extent_idx_t;

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

/* Inode structure.  The legacy ext2 layout ends at offset 128
 * (i_osd2[]'s last byte).  Modern ext4 (mkfs default) uses 256-byte
 * inodes — the post-128 region holds *_extra fields for nanosecond
 * timestamps and per-object checksums.  Whether these fields are
 * actually meaningful on a given mount depends on fs->inode_size and
 * the per-inode i_extra_isize byte (which says how many of the post-
 * 128 bytes the on-disk format defines).  Read/write code must clamp
 * the byte count it touches to fs->inode_size to avoid corrupting
 * neighbouring inodes on a 128-byte-inode mount.  */
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
    /* --- offset 128: ext4 extended fields, valid iff inode_size > 128 --- */
    uint16_t i_extra_isize;       /* 128: bytes used past offset 128   */
    uint16_t i_checksum_hi;       /* 130: high 16 bits of inode csum   */
    uint32_t i_ctime_extra;       /* 132: nsec << 2 | (sec >> 32 & 3)  */
    uint32_t i_mtime_extra;       /* 136                                */
    uint32_t i_atime_extra;       /* 140                                */
    uint32_t i_crtime;            /* 144: birthtime, seconds            */
    uint32_t i_crtime_extra;      /* 148: birthtime nsec encoding       */
} __attribute__((packed)) ext2_inode_t;

/* Helpers: split / pack the (sec,nsec) pair against the on-disk
 * (i_*time, i_*time_extra) encoding.
 *   - i_*time holds bits [31:0] of the second value (signed pre-1970,
 *     unsigned post-2038 when the extra extension bits are present).
 *   - i_*time_extra packs (nsec << 2) | (sec_high & 0x3) in 32 bits.
 *     The 2-bit sec_high lets the on-disk format reach year ~2446.
 *     We only carry 32-bit time_t in the kernel for now, so we just
 *     ignore the high bits on read and zero them on write.
 *
 * If extra is 0 (small inode, or pre-EXTRA_ISIZE on-disk image) then
 * nsec=0, sec=as-stored.  Encoding the kernel-side "no nsec known"
 * value to 0 round-trips that case correctly.  */
static inline uint32_t ext2_time_pack_extra(uint32_t nsec) {
    return (nsec & 0x3FFFFFFFu) << 2;
}
static inline uint32_t ext2_time_extra_nsec(uint32_t extra) {
    return extra >> 2;
}

// Directory Entry
typedef struct {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[];
} __attribute__((packed)) ext2_dirent_t;

// EXT2 filesystem context (per-mount)
struct mount;
typedef struct {
    fs_node_t *device;          // Block device node
    struct mount *mp;           // VFS mount structure
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

    // Active block group bitmap cache
    uint32_t active_bg_group;
    uint8_t *active_bg_bitmap;

    // Active inode group bitmap cache
    uint32_t active_inode_bg_group;
    uint8_t *active_inode_bg_bitmap;

    /* Mount flags — MNT_RDONLY refuses every write op.  Also set
     * automatically when the on-disk superblock has ROCOMPAT bits
     * we don't understand: writing might still work for code paths
     * we control, but the fs's invariants might depend on the
     * unknown feature, so we refuse.  */
    uint32_t mnt_flags;
    int      readonly;

    /* metadata_csum seed.  Either crc32c(~0, uuid, 16) (default) or
     * the explicit s_checksum_seed value if EXT2F_INCOMPAT_CSUM_SEED
     * is on.  Used for group-descriptor and (future) inode csums.  */
    uint32_t csum_seed;

    /* htree hash seed — four words at sb_buf+236 on disk.  Initial
     * state for the half_md4 and tea hash functions; legacy ignores
     * it.  Lifted from sb_buf at mount because it lives past the
     * end of our truncated ext2_superblock_t struct.  */
    uint32_t hash_seed[4];
} ext2_fs_t;

#define EXT2_DCACHE_SIZE 16

typedef struct {
    char name[64];
    uint8_t name_len;
    uint32_t inode_num;
} ext2_dcache_entry_t;

// EXT2 file/directory node context
typedef struct {
    ext2_fs_t *fs;
    uint32_t inode_num;
    ext2_inode_t inode;
    uint16_t cache_slot;
    uint16_t pin_count;
    struct dirent current_dirent; // For readdir

    // Readdir cache for sequential access optimization
    uint64_t last_readdir_idx;
    uint32_t last_readdir_pos;

    // Directory entry cache
    ext2_dcache_entry_t dcache[EXT2_DCACHE_SIZE];
    uint32_t dcache_idx;

    // Scratch buffers for I/O operations (protected by lock)
    mutex_t lock;
    uint8_t *block_buf;
    uint32_t *indirect_buf;
    uint32_t *dindirect_buf;
    uint32_t *tindirect_buf;
} ext2_node_t;

// Public functions
void ext2_init(void);
fs_node_t *ext2_mount(const char *device, uint32_t flags, void *data);

// Driver operations (non-static for extensibility/testing)
int ext2_read_inode(ext2_fs_t *fs, uint32_t inode_num, ext2_inode_t *inode);
int ext2_write_inode(ext2_fs_t *fs, uint32_t inode_num, ext2_inode_t *inode);
uint32_t ext2_read_block(ext2_fs_t *fs, uint32_t block_num, void *buffer);
uint32_t ext2_write_block(ext2_fs_t *fs, uint32_t block_num, const void *buffer);
// Optimized versions taking ext2_node_t for cached buffers
uint32_t ext2_inode_read(ext2_node_t *node, off_t offset, uint32_t size, void *buffer);
uint32_t ext2_inode_write(ext2_node_t *node, off_t offset, uint32_t size, const void *buffer);
uint32_t ext2_alloc_block(ext2_fs_t *fs);
void ext2_free_block(ext2_fs_t *fs, uint32_t block_num);
uint32_t ext2_alloc_inode(ext2_fs_t *fs, int is_dir);
void ext2_free_inode(ext2_fs_t *fs, uint32_t inode_num, int was_dir);
uint32_t ext2_read_blocks(ext2_fs_t *fs, uint32_t block_num, uint32_t count, void *buffer);
void ext2_get_blocks_extent(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t block_idx, uint32_t max_count, 
                                   uint32_t *phys_block, uint32_t *count,
                                   uint32_t *indirect_buf, uint32_t *dindirect_buf, uint32_t *tindirect_buf);
uint32_t ext2_get_block_num(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t block_idx,
                                   uint32_t *indirect_buf, uint32_t *dindirect_buf, uint32_t *tindirect_buf);

// VFS callbacks (non-static as requested)
size_t ext2_file_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer);
size_t ext2_file_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer);
struct dirent *ext2_readdir(fs_node_t *node, uint64_t index);
fs_node_t *ext2_finddir(fs_node_t *node, char *name);
int ext2_readlink(fs_node_t *node, char *buf, size_t size);
int ext2_truncate(fs_node_t *node, off_t length);
int ext2_mkdir(fs_node_t *node, const char *name, uint16_t permission);
int ext2_rmdir(fs_node_t *node, const char *name);
int ext2_unmount(fs_node_t *node);
int ext2_link(fs_node_t *parent, fs_node_t *source, const char *name);
int ext2_unlink(fs_node_t *node, const char *name);
int ext2_rename(fs_node_t *old_parent, const char *old_name, fs_node_t *new_parent, const char *new_name);
int ext2_statfs(fs_node_t *node, struct statfs *buf);

// Helpers
int ext2_find_next_zero_bit(void *bitmap, uint32_t total_bits, uint32_t start, uint32_t end, uint32_t *found_idx);
fs_node_t *ext2_alloc_node(ext2_fs_t *fs, uint32_t inode_num, ext2_inode_t *inode);

/* htree name hash — see ext2_hash.c for the port notes.  Returns 0
 * on success with *hash_major and (optionally) *hash_minor filled
 * in; returns -1 if the name is empty / too long / the hash version
 * isn't one we recognise.  */
int ext2_htree_hash(const char *name, int len, const uint32_t *hash_seed,
                    int hash_version, uint32_t *hash_major,
                    uint32_t *hash_minor);

/* xattr read-side — see ext2_xattr.c.  full_name is the namespaced
 * form: "user.foo", "security.selinux", "trusted.bar", etc.  out
 * may be NULL to query the byte count without reading.  Returns 0
 * on hit (or 0-byte list), -ENODATA on miss, -ERANGE if a non-NULL
 * out buffer was too small, -ENOTSUP for unknown namespace.  */
int ext2_xattr_get(fs_node_t *node, const char *full_name,
                   void *out, size_t out_size, size_t *result_size);
int ext2_xattr_list(fs_node_t *node, void *out, size_t out_size,
                    size_t *result_size);

#endif
