/*
 * sys/fs/sysv/sysv.h — read-only mount driver for the V7-derived
 * family of on-disk layouts: Xenix (incl. SCO Xenix 86), System V
 * release 2/3/4, and Coherent.  All three share enough structure
 * that one codebase handles them; we record which flavour we're
 * looking at in sysv_fs_t::variant and branch on it only at the
 * points the on-disk format actually differs:
 *
 *   - block size (Xenix V1: 512, Xenix V2 / SysV: 1024)
 *   - direct-block packing in the inode (Xenix uses 13 three-byte
 *     zone numbers in 40 bytes of i_addr; SysV uses 13 four-byte
 *     block numbers in 52 bytes)
 *   - superblock magic + magic location
 *
 * On-disk byte order is little-endian for all three on the i386
 * targets we care about, so we read with memcpy / direct field
 * access.  No swapping is needed.
 *
 * References:
 *   Bach, "The Design of the UNIX Operating System", §4 (V7 fs)
 *   Linux fs/sysv/sysv.h (canonical reference layout)
 *   SCO Xenix System V development system manual, FILSYS(F)
 */

#ifndef _SYSV_FS_H
#define _SYSV_FS_H

#include <stdint.h>
#include <vfs/vfs.h>

/* Variant selected by the mount-time magic probe. */
typedef enum sysv_variant {
    SYSV_VARIANT_NONE = 0,
    SYSV_VARIANT_XENIX,        /* Microsoft / SCO Xenix V7 fs (512 or 1024 byte blocks) */
    SYSV_VARIANT_SYSV4,        /* System V release 2/3/4 (1024 byte blocks) */
    SYSV_VARIANT_COHERENT,     /* Coherent (1024 byte blocks, 14-byte names) */
} sysv_variant_t;

/* Root inode is always inode #2 in the V7 family (#1 is bad
 * blocks, #0 unused). */
#define SYSV_ROOT_INO  2

/*
 * Superblock magic + location:
 *
 *   Xenix    — magic 0x012FF7B5 (little-endian: 0xb5 0xf7 0x2f 0x01)
 *              at offset 0x1F8 of a 1024-byte superblock that lives
 *              at logical block 1 (=offset 512 on disk).
 *   SysV 2/3 — magic 0xfd187e20 at offset 0x1F8.
 *   Coherent — no magic; identified by other signatures.  We
 *              don't auto-probe coherent today; only mount it if
 *              the user asks for "coherent" explicitly (not yet
 *              wired through the mount API).
 *
 * The "type" word at offset 0x1FC determines block size:
 *   1  → 512-byte blocks (Xenix V1 only)
 *   2  → 1024-byte blocks
 *   3  → 2048-byte blocks (rare; SysV AFS)
 */
#define SYSV_MAGIC_XENIX  0x012FF7B5U
#define SYSV_MAGIC_SYSV4  0xFD187E20U

#define SYSV_MAGIC_OFFSET 0x1F8
#define SYSV_TYPE_OFFSET  0x1FC

/*
 * Xenix on-disk superblock (truncated — we only care about fields
 * we'd actually read).  Total length 1024 bytes on disk; we read
 * the whole block and key off fixed offsets for magic/type so we
 * don't have to chase the variant-specific layout when probing.
 */
struct xenix_super_block {
    uint16_t s_isize;          /* zones reserved for inodes */
    uint32_t s_fsize;          /* total zones in filesystem */
    uint16_t s_nfree;          /* number of zones in free list */
    uint32_t s_free[100];      /* free zone list (cache) */
    uint16_t s_ninode;         /* number of free inodes in s_inode */
    uint16_t s_inode[100];     /* free inode cache */
    /* ... lock flags, time, etc. — not needed for read-only */
} __attribute__((packed));

/*
 * Xenix on-disk inode — 64 bytes.
 *
 *   i_addr[]:  zone numbers packed three bytes per entry.
 *     - i_addr[0..9]:   10 direct zone numbers
 *     - i_addr[10]:     single-indirect
 *     - i_addr[11]:     double-indirect
 *     - i_addr[12]:     triple-indirect
 *
 * Three-byte zones: little-endian byte order, the high 8 bits are
 * the third byte.  Use sysv_unpack_zone() to read.
 */
struct xenix_inode {
    uint16_t i_mode;
    uint16_t i_nlink;
    uint16_t i_uid;
    uint16_t i_gid;
    uint32_t i_size;
    uint8_t  i_addr[40];   /* 13 * 3 = 39 bytes + 1 byte padding */
    uint32_t i_atime;
    uint32_t i_mtime;
    uint32_t i_ctime;
} __attribute__((packed));

/*
 * SysV on-disk inode — 64 bytes total, same shape as Xenix but the
 * direct blocks are packed differently:
 *
 *   i_addr[0..9]: 10 direct, 3 bytes each = 30 bytes
 *   i_addr[10..12]: indirects, also 3 bytes each
 *
 * SysV historically packed 13 three-byte zones in 39 bytes too,
 * with the same unpack rule.  Some later SVR4 releases moved to
 * four-byte block numbers via "Veritas vxfs" but that's a
 * different filesystem altogether.  For our purposes Xenix and
 * SysV inodes use the same struct.
 */
struct sysv_inode {
    uint16_t i_mode;
    uint16_t i_nlink;
    uint16_t i_uid;
    uint16_t i_gid;
    uint32_t i_size;
    uint8_t  i_addr[40];
    uint32_t i_atime;
    uint32_t i_mtime;
    uint32_t i_ctime;
} __attribute__((packed));

/*
 * Directory entry — same in all three variants except for the
 * d_name array width (Coherent uses 14 bytes; Xenix and SysV use
 * 14 too historically, though SCO Xenix 286 / SCO Unix later
 * extended it).  We use 14 — matches Linux's sysv driver.
 */
#define SYSV_NAMELEN 14
struct sysv_dirent {
    uint16_t d_ino;
    char     d_name[SYSV_NAMELEN];
} __attribute__((packed));

/* Per-mount state.  Block size and layout are pinned at mount
 * time based on the variant + type word in the superblock. */
typedef struct sysv_fs {
    fs_node_t       *device;
    struct mount    *mp;
    sysv_variant_t   variant;
    uint32_t         block_size;       /* bytes per disk block */
    uint32_t         nblocks;          /* total blocks (s_fsize) */
    uint32_t         ninodes;          /* total inodes */
    uint32_t         first_data_block; /* first block holding data */
    uint32_t         inode_block_start;/* first block of the inode table */
    uint8_t          inode_size;       /* 64 for Xenix/SysV */
    uint8_t          name_len;         /* SYSV_NAMELEN today */
} sysv_fs_t;

/* Per-node state carried in fs_node_t::impl_data (no allocation
 * for now — fits inline since we just need the inode number and
 * mode + size cached). */
typedef struct sysv_node {
    sysv_fs_t *fs;
    uint32_t   ino;
    /* Direct/indirect block numbers, unpacked from i_addr at
     * inode-read time so file_read can index them without going
     * back to disk. */
    uint32_t   addr[13];
} sysv_node_t;

/* Driver entry points. */
fs_node_t *sysv_mount(const char *device, uint32_t flags, void *data);
void       sysv_init(void);

#endif /* _SYSV_FS_H */
