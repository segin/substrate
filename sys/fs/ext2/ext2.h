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
/* First inode a revision-0 filesystem makes available to files; revision 1+
 * carries the real value in s_first_ino.  Inodes 1..10 are reserved. */
#define EXT2_GOOD_OLD_FIRST_INO  11

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
/* EXT2-A5 (audit SB-04): META_BG is NOT supported — with meta_bg the
 * group descriptors are no longer one contiguous run after the
 * superblock (each metagroup keeps its own descriptor block, cutover at
 * s_first_meta_bg), and this driver reads AND flushes the GDT as a
 * contiguous table: on a >1-metagroup image that reads garbage
 * descriptors and writes descriptor blocks over file data.  Refuse the
 * mount until the layout is implemented. */
#define EXT2F_INCOMPAT_SUPP   (EXT2F_INCOMPAT_FTYPE | \
                               EXT2F_INCOMPAT_EXTENTS | \
                               EXT2F_INCOMPAT_FLEX_BG | \
                               EXT2F_INCOMPAT_CSUM_SEED | \
                               EXT2F_INCOMPAT_64BIT)
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
    uint16_t e_len;        /* raw <= 32768: INITIALIZED extent of
                              that length (32768 = EXT_INIT_MAX_LEN,
                              a value Linux routinely writes).
                              raw > 32768: UNINITIALIZED extent of
                              length raw - 32768 — covered blocks
                              are preallocated but never written
                              and MUST read as zeros. */
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

/* Block Group Descriptor — the 32-byte layout (low half of a 64-byte
 * descriptor on INCOMPAT_64BIT filesystems).  EXT2-A6 (audit BG-01/
 * SB-02/BG-03): the fields past bg_used_dirs_count were previously
 * bg_pad + bg_reserved[], hiding bg_flags (lazy-init state) and the
 * three checksum/accounting fields this driver must maintain. */
typedef struct {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_flags;                 /* EXT2_BG_* lazy-init flags */
    uint32_t bg_exclude_bitmap_lo;     /* snapshot exclude bitmap (unused) */
    uint16_t bg_block_bitmap_csum_lo;  /* crc32c(seed, bitmap) & 0xFFFF */
    uint16_t bg_inode_bitmap_csum_lo;
    uint16_t bg_itable_unused;         /* unused inode-table tail entries */
    uint16_t bg_checksum;              /* crc16 (gdt_csum) / crc32c lo16 */
} __attribute__((packed)) ext2_group_desc_t;

/* bg_flags bits (spec 2.3.2). */
#define EXT2_BG_INODE_UNINIT  0x0001   /* inode bitmap+table not initialized */
#define EXT2_BG_BLOCK_UNINIT  0x0002   /* block bitmap not initialized */
#define EXT2_BG_INODE_ZEROED  0x0004   /* on-disk inode table is zeroed */

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
    /* osd2, Linux flavor (spec 2.4.1).  Previously an opaque 12 bytes,
     * which hid the 32-bit uid/gid high halves — ownership above 65535
     * was silently truncated on every write. */
    uint16_t l_i_blocks_high;
    uint16_t l_i_file_acl_high;
    uint16_t l_i_uid_high;
    uint16_t l_i_gid_high;
    uint16_t l_i_checksum_lo;
    uint16_t l_i_reserved;
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
    /* Deferred metadata flush.  The block/inode bitmaps are written through
     * per allocation (authoritative on-disk state); the much costlier
     * superblock (primary + every sparse-super backup) and group-descriptor
     * free-count rewrites are deferred and coalesced — flushed on a threshold
     * of accumulated changes, on sync, and on unmount — instead of on every
     * single alloc/free.  A crash before a flush costs only fsck-fixable
     * free-count discrepancies, never data or allocation state. */
    uint32_t bgd_size;          // bytes actually kmalloc'd for bgd (EXT2-21:
                                // rounded up to whole blocks, so it is not
                                // group_count * sizeof(ext2_group_desc_t))
    uint8_t *bgd_dirty;         // per-group dirty bitmap (group_count bits)
    int      sb_dirty;          // superblock free counts need flushing
    uint32_t meta_dirty_ops;    // deferred metadata ops since last flush
    /* Serialises the block/inode allocator: ext2_alloc_block/_inode and
     * ext2_free_block/_inode read-modify-write shared state (the cached
     * active_bg_bitmap, bgd[] free counts, sb free counts, last_alloc hints).
     * Two allocations interleaving — on SMP, or on UP when one sleeps on the
     * bitmap-block I/O and another runs — can hand out the same block/inode or
     * write a stale bitmap.  All four allocator I/O helpers are lock-free so
     * holding this across their I/O cannot deadlock. */
    mutex_t  alloc_lock;
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
    int      force_readonly;   /* ro forced (unsupported RO_COMPAT); no rw remount */

    /* metadata_csum seed.  Either crc32c(~0, uuid, 16) (default) or
     * the explicit s_checksum_seed value if EXT2F_INCOMPAT_CSUM_SEED
     * is on.  Used for group-descriptor and (future) inode csums.  */
    uint32_t csum_seed;

    /* High halves of the per-group bitmap checksums, kept only when the
     * on-disk descriptor is big enough to carry them (desc_size >= 64).
     * fs->bgd holds just the low 32 bytes of each descriptor, so these
     * two fields would otherwise have nowhere to live between the
     * bitmap write that computes them and the deferred descriptor
     * flush that commits them.  NULL when unused. */
    uint16_t *bbitmap_csum_hi;
    uint16_t *ibitmap_csum_hi;

    /* htree hash seed — four words at sb_buf+236 on disk.  Initial
     * state for the half_md4 and tea hash functions; legacy ignores
     * it.  Lifted from sb_buf at mount because it lives past the
     * end of our truncated ext2_superblock_t struct.  */
    uint32_t hash_seed[4];

    /* i_extra_isize to stamp on inodes we create: the filesystem's
     * s_want_extra_isize when it is sane, else the 32 that mkfs.ext4
     * uses.  0 on a 128-byte-inode filesystem. */
    uint16_t want_extra_isize;

    /* INCOMPAT_FILETYPE: when clear, a dirent has NO file_type byte —
     * offset 7 is the high byte of a 16-bit name_len (spec 2.4.3.1),
     * so writing a type there corrupts the name length. */
    int      has_ftype;

    /* Layout facts lifted from the superblock at mount, needed to
     * synthesize the bitmap of a lazy-init (uninit) block group and to
     * place superblock backups: how many blocks the group-descriptor
     * table occupies, how many reserved GDT blocks follow it, and which
     * groups carry a backup on a sparse_super2 filesystem. */
    uint32_t gdt_blocks;
    uint32_t reserved_gdt_blocks;
    int      sparse_super2;
    uint32_t backup_bgs[2];

    /* On-disk group-descriptor size.  32 for legacy ext2/3 + ext4
     * without INCOMPAT_64BIT; 64 when 64BIT is on.  The bgd table
     * is read with this stride from disk; we still only KEEP 32
     * bytes per descriptor in fs->bgd because all our internal
     * addresses are uint32_t — anything that needs the high half
     * would have made the mount refuse to begin with.  */
    uint32_t desc_size;
} ext2_fs_t;

#define EXT2_DCACHE_SIZE 32

/* Sentinel inode in a dcache entry: a NEGATIVE (known-absent) result.  A real
 * ext2 inode is 1..s_inodes_count, never 0xFFFFFFFF, and 0 means "empty slot",
 * so this never collides.  Negative entries let a repeated lookup of a name
 * that does not exist in the directory (the dynamic linker / PATH search /
 * /perso/ shadow walk trying many directories) skip the full linear dir scan.
 * The existing add_entry/remove_entry invalidation clears any entry whose
 * inode_num != 0 matching the name, so a negative entry is dropped the moment
 * that name is created — no extra invalidation needed. */
#define EXT2_DCACHE_NEGATIVE 0xFFFFFFFFu

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
    /* Set by ext2_unlink when the dirent is removed but FDs are
     * still open against the inode.  POSIX requires the data to
     * remain accessible until the last close — when ext2_node_close
     * decrements pin_count to 0 and sees this flag, it frees the
     * on-disk blocks and inode that unlink deferred. */
    uint8_t orphaned;
    uint8_t was_dir_at_unlink;   /* preserved across deferred delete */
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
struct blkdev;
/* Read the ext2 volume label (s_volume_name) from a raw device. */
int ext2_read_label(struct blkdev *dev, char *label, size_t len);

// Driver operations (non-static for extensibility/testing)
int ext2_read_inode(ext2_fs_t *fs, uint32_t inode_num, ext2_inode_t *inode);
int ext2_write_inode(ext2_fs_t *fs, uint32_t inode_num, ext2_inode_t *inode);
/* Same, but for the first write of a freshly allocated inode: zeroes the
 * whole on-disk record (a recycled slot still carries the previous
 * file's inline xattrs) and stamps the filesystem's want_extra_isize. */
int ext2_write_inode_new(ext2_fs_t *fs, uint32_t inode_num, ext2_inode_t *inode);
uint32_t ext2_read_block(ext2_fs_t *fs, uint32_t block_num, void *buffer);
uint32_t ext2_write_block(ext2_fs_t *fs, uint32_t block_num, const void *buffer);
// Optimized versions taking ext2_node_t for cached buffers
uint32_t ext2_inode_read(ext2_node_t *node, off_t offset, uint32_t size, void *buffer);
/* Writes up to `size` bytes and returns the count actually written.  A short
 * or zero return needs a reason: `errp`, when non-NULL, receives 0 on full
 * success or a negative errno describing why the write stopped early
 * (-ENOSPC, -ENOMEM, -EINVAL).  Callers that report to userspace must consult
 * it -- returning 0 for a non-zero count is a POSIX violation (EXT2-15). */
uint32_t ext2_inode_write(ext2_node_t *node, off_t offset, uint32_t size,
                          const void *buffer, int *errp);
uint32_t ext2_alloc_block(ext2_fs_t *fs);
void ext2_free_block(ext2_fs_t *fs, uint32_t block_num);
/* Range free: one bitmap write per touched group (extent teardown). */
void ext2_free_blocks(ext2_fs_t *fs, uint32_t block_num, uint32_t count);
uint32_t ext2_alloc_inode(ext2_fs_t *fs, int is_dir);
void ext2_free_inode(ext2_fs_t *fs, uint32_t inode_num, int was_dir);
uint32_t ext2_read_block(ext2_fs_t *fs, uint32_t block_num, void *buffer);
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

extern uint64_t ext2_alloc_node_hits;
extern uint64_t ext2_alloc_node_new;
extern uint64_t ext2_alloc_node_fail;
extern uint64_t ext2_alloc_node_fail_pinned;
extern uint64_t ext2_alloc_node_fail_locked;
extern uint64_t ext2_finddir_calls;
extern uint64_t ext2_finddir_dcache_hit;
extern uint64_t ext2_finddir_walk_found;
extern uint64_t ext2_finddir_walk_missing;
extern uint64_t ext2_finddir_break_recv_malformed;
extern uint64_t ext2_finddir_break_block0;
extern uint64_t ext2_root_pin_lost;

#endif
