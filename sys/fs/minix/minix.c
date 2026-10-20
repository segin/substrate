#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <fs/minix/minix.h>
#include <kern/console.h>
#include <kern/time.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <vfs/vfs.h>
#include <vm/vm_kmem.h>

#define MINIX_BLOCK_SIZE 1024

/* Internal wrapper to hold inode data and readdir buffer */
struct minix_inode_wrapper {
    union {
        struct minix_inode_v1 v1;
        struct minix_inode_v2 v2;
    } inode;
    struct dirent dirent;
    int opens;      /* open_fs references (open files, working directories) */
    int is_root;    /* the mount's root: freed by unmount, not by close */
    int cached;     /* in minix_ncache: freed with its inode or volume */
    struct fs_node *ncache_next;    /* minix_ncache bucket chain */
};

/* Forward declarations */
static fs_node_t *minix_mount(const char *device, uint32_t flags, void *data);
static size_t minix_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer);
static struct dirent *minix_readdir(fs_node_t *node, uint64_t index);
static fs_node_t *minix_finddir(fs_node_t *node, char *name);
static size_t minix_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer);
static int minix_readlink(fs_node_t *node, char *buf, size_t size);
static int minix_symlink(fs_node_t *node, const char *target, const char *name);
static int minix_link(fs_node_t *dir, fs_node_t *node, const char *name);
static int minix_unlink(fs_node_t *dir, const char *name);
static int minix_rmdir(fs_node_t *dir, const char *name);
static int minix_mknod(fs_node_t *dir, const char *name, uint16_t mode, uint32_t dev);
static int minix_mkdir(fs_node_t *dir, const char *name, uint16_t permission);
static int minix_unmount(fs_node_t *root);
static int minix_dir_is_empty(fs_node_t *node);

/* Inode reading helper */
static int minix_read_inode(minix_fs_t *fs, uint32_t inode_num, fs_node_t *node);
static int minix_write_inode(minix_fs_t *fs, fs_node_t *node);
static int minix_write_inode_raw(minix_fs_t *fs, uint32_t inode_num, struct minix_inode_v1 *inode);
static void minix_free_block(minix_fs_t *fs, uint32_t zone);
static int minix_chmod(fs_node_t *node, uint32_t mode);

static uint16_t minix_type_bits_from_flags(uint32_t flags) {
    /* The FS_* types are values in the low three bits, not flags: tested
     * with '&', a symlink (6) matched FS_DIRECTORY (2) first and was written
     * with a directory's mode, and a FIFO (5) matched FS_FILE. */
    switch (flags & 0x7) {
    case FS_DIRECTORY:   return 0x4000;
    case FS_SYMLINK:     return 0xA000;
    case FS_CHARDEVICE:  return 0x2000;
    case FS_BLOCKDEVICE: return 0x6000;
    case FS_PIPE:        return 0x1000;
    case FS_FILE:        return 0x8000;
    case FS_SOCKET:      return 0xC000;
    default:             return 0;
    }
}

/* Filesystem definition */
static filesystem_t minix_fs = {
    .name = "minix",
    .mount = minix_mount,
    .next = NULL
};

void minix_init(void) {
    kprint("Initializing Minix FS Driver...\n");
    vfs_register_filesystem(&minix_fs);
}

/* Zone conversion helper */
/* Bounds-check a zone number that came from disk before using it as a
 * device offset.  s_nzones (v1) / s_zones (v2) gives the filesystem's
 * extent; anything outside that window is corrupt or hostile. */
/*
 * Mount accepts the 14-character-name variants (MINIX_V1_Magic_14
 * 0x138F, MINIX_V2_Magic_14 0x2478), but every directory routine hardcoded
 * sizeof(struct minix_dirent_v1) == 32.  Those variants use a 16-byte stride
 * (2-byte inode + 14-byte name), so readdir read entry 0 as {ino, name[30]}
 * spanning TWO on-disk entries: names carried the next entry's inode number,
 * every second entry was invisible, and d_ino alternated real and garbage --
 * straight into read_inode and unlink.  Derive both the stride and the name
 * length from the magic.
 */
static inline uint32_t minix_dirent_namelen(const minix_fs_t *fs) {
    return (fs->sb.s_magic == MINIX_V1_Magic_14 ||
            fs->sb.s_magic == MINIX_V2_Magic_14) ? 14u : 30u;
}

static inline uint32_t minix_dirent_size(const minix_fs_t *fs) {
    return 2u + minix_dirent_namelen(fs);   /* uint16 inode + name[] */
}

/*
 * minix_get_zone_v1/v2 used to validate the INDIRECT block pointer before
 * reading it but return buf[index] unchecked, so a corrupt indirect block
 * handed an arbitrary 16/32-bit zone number straight to the caller.  read_fs
 * then computed `minix_zone_off(zone)` in uint32_t: zone 0x00400000
 * truncates to offset 0, which returns the SUPERBLOCK on a read and lets
 * minix_write scribble over it.  Validate on the way out, and do the offset
 * arithmetic in 64 bits.
 */
/*
 * The zone bitmap is NOT indexed by zone number.  MINIX maps bit i
 * to zone i + s_firstdatazone - 1, i.e. bit 1 is the first data zone.  This
 * driver used the zone number directly, which is self-consistent as long as
 * only substrate touches the volume -- but fsck.minix and Linux read the same
 * bitmap with the correct mapping, see those bits as describing entirely
 * different zones, consider substrate's allocated zones free, and hand them
 * out again.  Silent cross-linking on any shared volume.
 *
 * Returns 0 for a zone below the first data zone (no bitmap bit describes it).
 */
static inline uint32_t minix_zone_bit(const minix_fs_t *fs, uint32_t zone) {
    uint32_t first = fs->sb.s_firstdatazone;
    if (first == 0 || zone < first) return 0;
    return zone - first + 1;
}

static inline uint32_t minix_bit_zone(const minix_fs_t *fs, uint32_t bit) {
    return bit + fs->sb.s_firstdatazone - 1;
}

static inline off_t minix_zone_off(uint32_t zone) {
    return (off_t)zone * (off_t)MINIX_BLOCK_SIZE;
}

static inline uint32_t minix_zone_ok(minix_fs_t *fs, uint32_t z) {
    uint32_t max = fs->sb.s_zones ? fs->sb.s_zones : fs->sb.s_nzones;
    if (max == 0) return 0;        /* unknown size — be conservative */
    return (z != 0 && z < max);
}

/* A zone that may hold file data or an indirect block: inside the volume
 * and past its metadata.  Zone numbers come off the disk, so one that is
 * not would have an indirect walk read -- and free the "zones" listed in --
 * a bitmap, an inode block or the superblock. */
static inline int minix_data_zone(minix_fs_t *fs, uint32_t z) {
    return minix_zone_ok(fs, z) && z >= fs->sb.s_firstdatazone;
}

/* Read zone `z` (one block) into buf if it is a data zone; else nothing. */
static size_t minix_read_zone(minix_fs_t *fs, uint32_t z, uint8_t *buf) {
    if (!minix_data_zone(fs, z)) return 0;
    return read_fs(fs->block_device, minix_zone_off(z), MINIX_BLOCK_SIZE, buf);
}

static uint32_t minix_get_zone_v1(minix_fs_t *fs, struct minix_inode_v1 *inode, uint32_t zone_index) {
    // Direct zones (0-6).  Every zone number comes off the disk, the
    // inode's own included: one past the volume is 0 ("a hole").
    if (zone_index < 7) {
        uint32_t z = inode->i_zone[zone_index];
        return minix_zone_ok(fs, z) ? z : 0;
    }

    uint32_t indirect_index = zone_index - 7;
    uint32_t entries_per_block = MINIX_BLOCK_SIZE / sizeof(uint16_t); // 512

    // Indirect zone (7)
    if (indirect_index < entries_per_block) {
        uint16_t z = inode->i_zone[7];
        if (!minix_zone_ok(fs, z)) return 0;

        uint16_t buf[MINIX_BLOCK_SIZE / 2];
        if (read_fs(fs->block_device, minix_zone_off(z), MINIX_BLOCK_SIZE, (uint8_t *)buf) != MINIX_BLOCK_SIZE) {
            return 0;
        }
        {
            uint32_t out = buf[indirect_index];      /* on-disk: validate */
            return minix_zone_ok(fs, out) ? out : 0;
        }
    }

    // Double indirect zone (8)
    uint32_t double_indirect_index = indirect_index - entries_per_block;
    if (double_indirect_index < entries_per_block * entries_per_block) {
        uint16_t z = inode->i_zone[8];
        if (!minix_zone_ok(fs, z)) return 0;

        uint16_t buf[MINIX_BLOCK_SIZE / 2];
        if (read_fs(fs->block_device, minix_zone_off(z), MINIX_BLOCK_SIZE, (uint8_t *)buf) != MINIX_BLOCK_SIZE) {
            return 0;
        }

        uint16_t indirect_block = buf[double_indirect_index / entries_per_block];
        if (!minix_zone_ok(fs, indirect_block)) return 0;

        if (read_fs(fs->block_device, minix_zone_off(indirect_block), MINIX_BLOCK_SIZE, (uint8_t *)buf) != MINIX_BLOCK_SIZE) {
            return 0;
        }

        {
            uint32_t out = buf[double_indirect_index % entries_per_block];
            return minix_zone_ok(fs, out) ? out : 0;
        }
    }

    return 0;
}


static uint32_t minix_get_zone_v2(minix_fs_t *fs, struct minix_inode_v2 *inode, uint32_t zone_index) {
    // Direct zones (0-6): validated like the rest (see v1).
    if (zone_index < 7) {
        uint32_t z = inode->i_zone[zone_index];
        return minix_zone_ok(fs, z) ? z : 0;
    }

    uint32_t indirect_index = zone_index - 7;
    uint32_t entries_per_block = MINIX_BLOCK_SIZE / sizeof(uint32_t); // 256 for V2

    // Indirect zone (7)
    if (indirect_index < entries_per_block) {
        uint32_t z = inode->i_zone[7];
        if (!minix_zone_ok(fs, z)) return 0;

        uint32_t buf[MINIX_BLOCK_SIZE / 4];
        if (read_fs(fs->block_device, minix_zone_off(z), MINIX_BLOCK_SIZE, (uint8_t *)buf) != MINIX_BLOCK_SIZE) {
            return 0;
        }
        {
            uint32_t out = buf[indirect_index];      /* on-disk: validate */
            return minix_zone_ok(fs, out) ? out : 0;
        }
    }

    // Double indirect zone (8)
    uint32_t double_indirect_index = indirect_index - entries_per_block;
    if (double_indirect_index < entries_per_block * entries_per_block) {
        uint32_t z = inode->i_zone[8];
        if (!minix_zone_ok(fs, z)) return 0;

        uint32_t buf[MINIX_BLOCK_SIZE / 4];
        if (read_fs(fs->block_device, minix_zone_off(z), MINIX_BLOCK_SIZE, (uint8_t *)buf) != MINIX_BLOCK_SIZE) {
            return 0;
        }

        uint32_t indirect_block = buf[double_indirect_index / entries_per_block];
        if (!minix_zone_ok(fs, indirect_block)) return 0;

        if (read_fs(fs->block_device, minix_zone_off(indirect_block), MINIX_BLOCK_SIZE, (uint8_t *)buf) != MINIX_BLOCK_SIZE) {
            return 0;
        }

        {
            uint32_t out = buf[double_indirect_index % entries_per_block];
            return minix_zone_ok(fs, out) ? out : 0;
        }
    }

    // Triple indirect zone (9) - Not supported/implemented yet for V2
    return 0;
}

static uint32_t minix_get_zone(minix_fs_t *fs, fs_node_t *node, uint32_t zone_index) {
    if (!node->ptr) return 0;

    if (fs->sb.s_magic == MINIX_V2_Magic || fs->sb.s_magic == MINIX_V2_Magic_14) {
        return minix_get_zone_v2(fs, (struct minix_inode_v2 *)node->ptr, zone_index);
    } else {
        return minix_get_zone_v1(fs, (struct minix_inode_v1 *)node->ptr, zone_index);
    }
}

static uint32_t minix_total_zones(minix_fs_t *fs) {
    if (fs->sb.s_magic == MINIX_V1_Magic || fs->sb.s_magic == MINIX_V1_Magic_14) {
        return fs->sb.s_nzones;
    }
    return fs->sb.s_zones;
}

static int minix_zero_zone(minix_fs_t *fs, uint32_t zone) {
    uint8_t zero_buf[MINIX_BLOCK_SIZE];
    memset(zero_buf, 0, sizeof(zero_buf));

    if (write_fs(fs->block_device, minix_zone_off(zone), MINIX_BLOCK_SIZE, zero_buf) != MINIX_BLOCK_SIZE) {
        return -1;
    }
    return 0;
}

/* Total/free space report.  Walks both the zone and the inode
 * bitmaps once per call; for a typical MINIX volume those bitmaps
 * are a handful of blocks, so this is cheap. */
static int minix_statfs(fs_node_t *node, struct statfs *buf) {
    if (!node || !buf) return -EINVAL;
    minix_fs_t *fs = (minix_fs_t *)(uintptr_t)node->impl;
    if (!fs) return -EINVAL;

    uint32_t total_zones  = minix_total_zones(fs);
    uint32_t first_data   = fs->sb.s_firstdatazone;
    uint32_t total_inodes = fs->sb.s_ninodes;

    uint8_t  bm[MINIX_BLOCK_SIZE];
    uint32_t free_zones = 0, free_inodes = 0;

    /* Zone bitmap starts after the 2 boot/super blocks and the inode bitmap.
     * Bit i covers zone i + s_firstdatazone - 1, NOT zone i, so
     * counting bit N as zone N reported a free count for the wrong range of
     * zones entirely.  Bit 0 is not a data zone. */
    uint32_t zmap_start = 2 + fs->sb.s_imap_blocks;
    for (uint32_t b = 0; b < fs->sb.s_zmap_blocks; b++) {
        if (read_fs(fs->block_device,
                    (zmap_start + b) * MINIX_BLOCK_SIZE,
                    MINIX_BLOCK_SIZE, bm) != MINIX_BLOCK_SIZE)
            break;
        for (uint32_t i = 0; i < MINIX_BLOCK_SIZE * 8; i++) {
            uint32_t bit = b * MINIX_BLOCK_SIZE * 8 + i;
            if (bit == 0) continue;                 /* bit 0 names no zone */
            uint32_t zone = minix_bit_zone(fs, bit);
            if (zone < first_data) continue;
            if (zone >= total_zones) goto zmap_done;
            if (!(bm[i / 8] & (1u << (i % 8))))
                free_zones++;
        }
    }
zmap_done:;

    /* Inode bitmap is right after the two boot/super blocks; bit 0 is
     * the "no inode" sentinel, bits 1..s_ninodes are real inodes. */
    uint32_t imap_start = 2;
    for (uint32_t b = 0; b < fs->sb.s_imap_blocks; b++) {
        if (read_fs(fs->block_device,
                    (imap_start + b) * MINIX_BLOCK_SIZE,
                    MINIX_BLOCK_SIZE, bm) != MINIX_BLOCK_SIZE)
            break;
        for (uint32_t i = 0; i < MINIX_BLOCK_SIZE * 8; i++) {
            uint32_t ino = b * MINIX_BLOCK_SIZE * 8 + i;
            if (ino < 1) continue;
            if (ino > total_inodes) goto imap_done;
            if (!(bm[i / 8] & (1u << (i % 8))))
                free_inodes++;
        }
    }
imap_done:;

    memset(buf, 0, sizeof(*buf));
    uint32_t zone_size = MINIX_BLOCK_SIZE << fs->sb.s_log_zone_size;
    buf->f_bsize  = zone_size;
    buf->f_iosize = zone_size;
    buf->f_blocks = total_zones > first_data ? total_zones - first_data : 0;
    buf->f_bfree  = free_zones;
    buf->f_bavail = free_zones;
    buf->f_files  = total_inodes;
    buf->f_ffree  = free_inodes;

    bool v2 = (fs->sb.s_magic == MINIX_V2_Magic ||
               fs->sb.s_magic == MINIX_V2_Magic_14);
    strncpy(buf->f_fstypename, v2 ? "minix2" : "minix",
            sizeof(buf->f_fstypename));
    return 0;
}

static uint32_t minix_alloc_zone(minix_fs_t *fs) {
    uint32_t first_data_zone = fs->sb.s_firstdatazone;
    uint32_t max_zones = minix_total_zones(fs);
    uint32_t zmap_start_block = 2 + fs->sb.s_imap_blocks;

    if (max_zones <= first_data_zone) return 0;

    uint32_t range = max_zones - first_data_zone;
    uint32_t start_zone = fs->last_zone_alloc;
    if (start_zone < first_data_zone || start_zone >= max_zones) {
        start_zone = first_data_zone;
    }

    for (uint32_t attempt = 0; attempt < range; attempt++) {
        uint32_t zone = first_data_zone + ((start_zone - first_data_zone + attempt) % range);
        uint32_t bit = minix_zone_bit(fs, zone);          /* bit != zone */
        if (bit == 0) continue;
        uint32_t block_index = bit / (MINIX_BLOCK_SIZE * 8);
        uint32_t bit_offset = bit % (MINIX_BLOCK_SIZE * 8);

        if (block_index >= fs->sb.s_zmap_blocks) continue;

        uint8_t buf[MINIX_BLOCK_SIZE];
        uint32_t zmap_block = zmap_start_block + block_index;

        if (read_fs(fs->block_device, zmap_block * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, buf) != MINIX_BLOCK_SIZE) {
            continue;
        }

        uint8_t mask = (uint8_t)(1u << (bit_offset % 8));
        uint32_t byte_index = bit_offset / 8;

        if (buf[byte_index] & mask) {
            continue;
        }

        buf[byte_index] |= mask;
        if (write_fs(fs->block_device, zmap_block * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, buf) != MINIX_BLOCK_SIZE) {
            continue;
        }

        if (minix_zero_zone(fs, zone) != 0) {
            buf[byte_index] &= (uint8_t)~mask;
            write_fs(fs->block_device, zmap_block * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, buf);
            continue;
        }

        fs->last_zone_alloc = zone + 1;
        if (fs->last_zone_alloc >= max_zones) {
            fs->last_zone_alloc = first_data_zone;
        }
        return zone;
    }

    return 0;
}

static int minix_set_zone_v1(minix_fs_t *fs, struct minix_inode_v1 *inode, uint32_t zone_index, uint32_t zone) {
    uint32_t entries_per_block = MINIX_BLOCK_SIZE / sizeof(uint16_t);

    if (zone > 0xFFFFU) return -1;

    if (zone_index < 7) {
        inode->i_zone[zone_index] = (uint16_t)zone;
        return 0;
    }

    uint32_t indirect_index = zone_index - 7;
    if (indirect_index < entries_per_block) {
        if (inode->i_zone[7] == 0) {
            uint32_t ind_zone = minix_alloc_zone(fs);
            if (ind_zone == 0 || ind_zone > 0xFFFFU) return -1;
            inode->i_zone[7] = (uint16_t)ind_zone;
        }
        /* A pointer read off the disk must name a data zone, or the
         * write below lands on metadata (the superblock, a bitmap). */
        if (!minix_data_zone(fs, inode->i_zone[7])) return -1;

        uint16_t buf[MINIX_BLOCK_SIZE / 2];
        if (read_fs(fs->block_device, minix_zone_off(inode->i_zone[7]), MINIX_BLOCK_SIZE, (uint8_t *)buf) != MINIX_BLOCK_SIZE) {
            return -1;
        }

        buf[indirect_index] = (uint16_t)zone;
        if (write_fs(fs->block_device, minix_zone_off(inode->i_zone[7]), MINIX_BLOCK_SIZE, (uint8_t *)buf) != MINIX_BLOCK_SIZE) {
            return -1;
        }
        return 0;
    }

    uint32_t double_indirect_index = indirect_index - entries_per_block;
    if (double_indirect_index < entries_per_block * entries_per_block) {
        if (inode->i_zone[8] == 0) {
            uint32_t dbl_zone = minix_alloc_zone(fs);
            if (dbl_zone == 0 || dbl_zone > 0xFFFFU) return -1;
            inode->i_zone[8] = (uint16_t)dbl_zone;
        }
        if (!minix_data_zone(fs, inode->i_zone[8])) return -1;

        uint16_t l1[MINIX_BLOCK_SIZE / 2];
        if (read_fs(fs->block_device, minix_zone_off(inode->i_zone[8]), MINIX_BLOCK_SIZE, (uint8_t *)l1) != MINIX_BLOCK_SIZE) {
            return -1;
        }

        uint32_t l1_index = double_indirect_index / entries_per_block;
        uint32_t l2_index = double_indirect_index % entries_per_block;

        if (l1[l1_index] == 0) {
            uint32_t l2_zone = minix_alloc_zone(fs);
            if (l2_zone == 0 || l2_zone > 0xFFFFU) return -1;
            l1[l1_index] = (uint16_t)l2_zone;
            if (write_fs(fs->block_device, minix_zone_off(inode->i_zone[8]), MINIX_BLOCK_SIZE, (uint8_t *)l1) != MINIX_BLOCK_SIZE) {
                minix_free_block(fs, l2_zone);
                return -1;
            }
        }
        if (!minix_data_zone(fs, l1[l1_index])) return -1;

        uint16_t l2[MINIX_BLOCK_SIZE / 2];
        if (read_fs(fs->block_device, minix_zone_off(l1[l1_index]), MINIX_BLOCK_SIZE, (uint8_t *)l2) != MINIX_BLOCK_SIZE) {
            return -1;
        }
        l2[l2_index] = (uint16_t)zone;
        if (write_fs(fs->block_device, minix_zone_off(l1[l1_index]), MINIX_BLOCK_SIZE, (uint8_t *)l2) != MINIX_BLOCK_SIZE) {
            return -1;
        }
        return 0;
    }

    return -1;
}

static int minix_set_zone_v2(minix_fs_t *fs, struct minix_inode_v2 *inode, uint32_t zone_index, uint32_t zone) {
    uint32_t entries_per_block = MINIX_BLOCK_SIZE / sizeof(uint32_t);

    if (zone_index < 7) {
        inode->i_zone[zone_index] = zone;
        return 0;
    }

    uint32_t indirect_index = zone_index - 7;
    if (indirect_index < entries_per_block) {
        if (inode->i_zone[7] == 0) {
            uint32_t ind_zone = minix_alloc_zone(fs);
            if (ind_zone == 0) return -1;
            inode->i_zone[7] = ind_zone;
        }
        if (!minix_data_zone(fs, inode->i_zone[7])) return -1;   /* see v1 */

        uint32_t buf[MINIX_BLOCK_SIZE / 4];
        if (read_fs(fs->block_device, minix_zone_off(inode->i_zone[7]), MINIX_BLOCK_SIZE, (uint8_t *)buf) != MINIX_BLOCK_SIZE) {
            return -1;
        }

        buf[indirect_index] = zone;
        if (write_fs(fs->block_device, minix_zone_off(inode->i_zone[7]), MINIX_BLOCK_SIZE, (uint8_t *)buf) != MINIX_BLOCK_SIZE) {
            return -1;
        }
        return 0;
    }

    uint32_t double_indirect_index = indirect_index - entries_per_block;
    if (double_indirect_index < entries_per_block * entries_per_block) {
        if (inode->i_zone[8] == 0) {
            uint32_t dbl_zone = minix_alloc_zone(fs);
            if (dbl_zone == 0) return -1;
            inode->i_zone[8] = dbl_zone;
        }
        if (!minix_data_zone(fs, inode->i_zone[8])) return -1;

        uint32_t l1[MINIX_BLOCK_SIZE / 4];
        if (read_fs(fs->block_device, minix_zone_off(inode->i_zone[8]), MINIX_BLOCK_SIZE, (uint8_t *)l1) != MINIX_BLOCK_SIZE) {
            return -1;
        }

        uint32_t l1_index = double_indirect_index / entries_per_block;
        uint32_t l2_index = double_indirect_index % entries_per_block;

        if (l1[l1_index] == 0) {
            uint32_t l2_zone = minix_alloc_zone(fs);
            if (l2_zone == 0) return -1;
            l1[l1_index] = l2_zone;
            if (write_fs(fs->block_device, minix_zone_off(inode->i_zone[8]), MINIX_BLOCK_SIZE, (uint8_t *)l1) != MINIX_BLOCK_SIZE) {
                minix_free_block(fs, l2_zone);
                return -1;
            }
        }
        if (!minix_data_zone(fs, l1[l1_index])) return -1;

        uint32_t l2[MINIX_BLOCK_SIZE / 4];
        if (read_fs(fs->block_device, minix_zone_off(l1[l1_index]), MINIX_BLOCK_SIZE, (uint8_t *)l2) != MINIX_BLOCK_SIZE) {
            return -1;
        }
        l2[l2_index] = zone;
        if (write_fs(fs->block_device, minix_zone_off(l1[l1_index]), MINIX_BLOCK_SIZE, (uint8_t *)l2) != MINIX_BLOCK_SIZE) {
            return -1;
        }
        return 0;
    }

    return -1;
}

static int minix_set_zone(minix_fs_t *fs, fs_node_t *node, uint32_t zone_index, uint32_t zone) {
    if (!node || !node->ptr) return -1;
    if (fs->sb.s_magic == MINIX_V2_Magic || fs->sb.s_magic == MINIX_V2_Magic_14) {
        return minix_set_zone_v2(fs, (struct minix_inode_v2 *)node->ptr, zone_index, zone);
    }
    return minix_set_zone_v1(fs, (struct minix_inode_v1 *)node->ptr, zone_index, zone);
}

static int minix_ensure_zone(minix_fs_t *fs, fs_node_t *node, uint32_t zone_index, uint32_t *zone_out) {
    uint32_t zone = minix_get_zone(fs, node, zone_index);
    if (zone != 0) {
        if (zone_out) *zone_out = zone;
        return 0;
    }

    uint32_t new_zone = minix_alloc_zone(fs);
    if (new_zone == 0) return -1;

    if (minix_set_zone(fs, node, zone_index, new_zone) != 0) {
        minix_free_block(fs, new_zone);
        return -1;
    }

    if (zone_out) *zone_out = new_zone;
    return 1;
}


/*
 * Every lookup hands out its own node (and inode wrapper), and nothing ever
 * freed them.  open_fs/close_fs bracket each real use -- an open file, a
 * working directory -- so a node is freed at its last close.  (A node that
 * is only looked up and never opened is still not reclaimed.)
 */
static void minix_node_open(fs_node_t *node) {
    struct minix_inode_wrapper *w = (struct minix_inode_wrapper *)node->ptr;
    if (w) __sync_fetch_and_add(&w->opens, 1);
}

/* Free a node minix_finddir handed out (its wrapper and the node). */
static void minix_node_destroy(fs_node_t *node) {
    if (!node) return;
    if (node->ptr) kfree(node->ptr, sizeof(struct minix_inode_wrapper));
    kfree(node, sizeof(*node));
}

/*
 * Lookup cache: one node per (volume, inode).  Path walks look every
 * component up and never close the nodes they pass through, so a node per
 * lookup leaked on every open; a lookup now returns the inode's cached
 * node, held (`opens`, which a walk's reference counts in too, so a node
 * dropped while a walk stands in it is freed at the walk's release, not
 * under it).  Nothing is evicted.  An entry goes only when its inode is freed (so a new file reusing
 * the number does not get the old node) or its volume is unmounted.  The
 * cache is bounded by the inodes looked up, at most the volume's count.
 */
#define MINIX_NCACHE_BUCKETS 256
static fs_node_t *minix_ncache[MINIX_NCACHE_BUCKETS];
static spinlock_t minix_ncache_lock = SPINLOCK_INIT("minix_ncache");

static struct minix_inode_wrapper *minix_w(fs_node_t *node) {
    return node ? (struct minix_inode_wrapper *)node->ptr : NULL;
}

static unsigned int minix_ncache_bucket(minix_fs_t *fs, uint64_t ino) {
    return (unsigned int)(((uintptr_t)fs >> 4) ^ ino) % MINIX_NCACHE_BUCKETS;
}

/* The cached node for (fs, ino), or NULL; the lock is held. */
static fs_node_t *minix_ncache_find_locked(minix_fs_t *fs, uint64_t ino) {
    for (fs_node_t *n = minix_ncache[minix_ncache_bucket(fs, ino)]; n;
         n = minix_w(n)->ncache_next) {
        if (n->impl == (uintptr_t)fs && n->inode == ino) return n;
    }
    return NULL;
}

/* The cached node for (fs, ino), or NULL. */
static fs_node_t *minix_ncache_find(minix_fs_t *fs, uint32_t ino) {
    spinlock_acquire(&minix_ncache_lock);
    fs_node_t *hit = minix_ncache_find_locked(fs, ino);
    spinlock_release(&minix_ncache_lock);
    return hit;
}

/* The same, held (a lookup reference: minix_node_close drops it).  Taken
 * under the cache lock, so minix_ncache_drop cannot free it first. */
static fs_node_t *minix_ncache_find_held(minix_fs_t *fs, uint32_t ino) {
    spinlock_acquire(&minix_ncache_lock);
    fs_node_t *hit = minix_ncache_find_locked(fs, ino);
    if (hit) minix_w(hit)->opens++;
    spinlock_release(&minix_ncache_lock);
    return hit;
}

/* Cache `node` (fresh from minix_read_inode).  Returns the node to use, held:
 * an entry another lookup cached meanwhile, or `node` itself, now cached. */
static fs_node_t *minix_ncache_insert(minix_fs_t *fs, fs_node_t *node) {
    spinlock_acquire(&minix_ncache_lock);
    fs_node_t *existing = minix_ncache_find_locked(fs, node->inode);
    if (!existing) {
        unsigned int b = minix_ncache_bucket(fs, node->inode);
        minix_w(node)->ncache_next = minix_ncache[b];
        minix_w(node)->cached = 1;
        minix_w(node)->opens = 1;
        minix_ncache[b] = node;
    } else {
        minix_w(existing)->opens++;
    }
    spinlock_release(&minix_ncache_lock);
    if (existing) {
        minix_node_destroy(node);
        return existing;
    }
    return node;
}

/* Unlink `node` from its bucket; the lock is held. */
static void minix_ncache_unlink_locked(fs_node_t *node) {
    struct minix_inode_wrapper *w = minix_w(node);
    fs_node_t **pp = &minix_ncache[minix_ncache_bucket(
        (minix_fs_t *)(uintptr_t)node->impl, node->inode)];

    while (*pp) {
        if (*pp == node) {
            *pp = w->ncache_next;
            break;
        }
        pp = &minix_w(*pp)->ncache_next;
    }
    w->ncache_next = NULL;
    w->cached = 0;
}

/* Drop `node` from the cache; free it now unless it is open (its last
 * close frees it then). */
static void minix_ncache_drop(fs_node_t *node) {
    struct minix_inode_wrapper *w = minix_w(node);
    if (!w) return;
    spinlock_acquire(&minix_ncache_lock);
    if (w->cached) minix_ncache_unlink_locked(node);
    int open = w->opens > 0 || w->is_root;
    spinlock_release(&minix_ncache_lock);
    if (!open) minix_node_destroy(node);
}

/* The inode is being freed: forget any node cached for it. */
static void minix_ncache_forget(minix_fs_t *fs, uint32_t ino) {
    fs_node_t *n = minix_ncache_find(fs, ino);
    if (n) minix_ncache_drop(n);
}

static void minix_node_close(fs_node_t *node) {
    struct minix_inode_wrapper *w = (struct minix_inode_wrapper *)node->ptr;
    if (!w || w->is_root) return;
    if (__sync_sub_and_fetch(&w->opens, 1) > 0) return;
    if (w->cached) return;      /* the cache keeps it */
    node->ptr = NULL;
    kfree(w, sizeof(*w));
    kfree(node, sizeof(*node));
}

static int minix_read_inode(minix_fs_t *fs, uint32_t inode_num, fs_node_t *node) {
    if (inode_num == 0 || inode_num > fs->sb.s_ninodes) return -1;

    // Calculate block and offset
    uint32_t inode_start_block = 2 + fs->sb.s_imap_blocks + fs->sb.s_zmap_blocks;

    bool v2 = (fs->sb.s_magic == MINIX_V2_Magic || fs->sb.s_magic == MINIX_V2_Magic_14);
    uint32_t inode_size = v2 ? sizeof(struct minix_inode_v2) : sizeof(struct minix_inode_v1);
    uint32_t inodes_per_block = MINIX_BLOCK_SIZE / inode_size;

    uint32_t block = inode_start_block + (inode_num - 1) / inodes_per_block;
    uint32_t offset = ((inode_num - 1) % inodes_per_block) * inode_size;

    uint8_t buf[MINIX_BLOCK_SIZE];
    if (read_fs(fs->block_device, block * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, buf) != MINIX_BLOCK_SIZE) {
        return -1;
    }

    node->inode = inode_num;
    node->impl = (uintptr_t)fs;
    node->statfs = minix_statfs;
    uint16_t mode;

    void *cache = kmalloc(sizeof(struct minix_inode_wrapper));
    if (!cache) return -1;
    memset(cache, 0, sizeof(struct minix_inode_wrapper));

    if (v2) {
        struct minix_inode_v2 *raw = (struct minix_inode_v2 *)(buf + offset);
        memcpy(cache, raw, inode_size);

        mode = raw->i_mode;
        node->length = raw->i_size;
        node->uid = raw->i_uid;
        node->gid = raw->i_gid;
        node->atime = raw->i_atime;
        node->mtime = raw->i_mtime;
        node->ctime = raw->i_ctime;
    } else {
        struct minix_inode_v1 *raw = (struct minix_inode_v1 *)(buf + offset);
        memcpy(cache, raw, inode_size);

        mode = raw->i_mode;
        node->length = raw->i_size;
        node->uid = raw->i_uid;
        node->gid = raw->i_gid;
        node->atime = raw->i_time;
        node->mtime = raw->i_time;
        node->ctime = raw->i_time;
    }
    
    node->ptr = (struct fs_node *)cache;

    // Parse mode
    if ((mode & 0xF000) == 0x4000) node->flags = FS_DIRECTORY;
    else if ((mode & 0xF000) == 0x8000) node->flags = FS_FILE;
    else if ((mode & 0xF000) == 0xA000) node->flags = FS_SYMLINK;
    else if ((mode & 0xF000) == 0x2000) {
        node->flags = FS_CHARDEVICE;
        if (v2) node->rdev = ((struct minix_inode_v2*)cache)->i_zone[0];
        else node->rdev = ((struct minix_inode_v1*)cache)->i_zone[0];
    } else if ((mode & 0xF000) == 0x6000) {
        node->flags = FS_BLOCKDEVICE;
        if (v2) node->rdev = ((struct minix_inode_v2*)cache)->i_zone[0];
        else node->rdev = ((struct minix_inode_v1*)cache)->i_zone[0];
    } else if ((mode & 0xF000) == 0x1000) node->flags = FS_PIPE;
    else if ((mode & 0xF000) == 0xC000) node->flags = FS_SOCKET;
    else node->flags = 0;

    node->mask = mode & 0xFFF;
    node->chmod = minix_chmod;
    node->open = minix_node_open;
    node->close = minix_node_close;
    /* The open count is what keeps a node from being freed: it is the
     * lookup reference too. */
    node->hold = minix_node_open;
    node->rele = minix_node_close;

    // Hook operations (the type is a value: see minix_type_bits_from_flags)
    uint32_t type = node->flags & 0x7;
    if (type == FS_DIRECTORY) {
        node->readdir = minix_readdir;
        node->finddir = minix_finddir;
    } else {
        node->read = minix_read;
        node->write = minix_write;
        if (type == FS_SYMLINK) {
             node->readlink = minix_readlink;
        }
    }
    // Hook directory ops provided by VFS (create, mkdir, etc) if supported
    if (type == FS_DIRECTORY) {
         node->symlink = minix_symlink;
         node->mkdir = minix_mkdir;
         node->link = minix_link;
         node->unlink = minix_unlink;
            node->rmdir = minix_rmdir;
         node->mknod = minix_mknod;
    }

    return 0;
}

static int minix_chmod(fs_node_t *node, uint32_t mode) {
    minix_fs_t *fs;

    if (!node) return -EINVAL;

    fs = (minix_fs_t *)(uintptr_t)node->impl;
    if (!fs) return -EINVAL;

    node->mask = mode & 07777U;
    if (minix_write_inode(fs, node) != 0) {
        return -EIO;
    }

    return 0;
}

static void minix_free_block(minix_fs_t *fs, uint32_t zone) {
    if (!minix_data_zone(fs, zone)) return;     /* nothing to free */

    uint32_t zmap_start_block = 2 + fs->sb.s_imap_blocks;
    uint32_t bit = minix_zone_bit(fs, zone);              /* bit != zone */
    if (bit == 0) return;
    uint32_t block_index = bit / (MINIX_BLOCK_SIZE * 8);
    uint32_t bit_offset = bit % (MINIX_BLOCK_SIZE * 8);

    if (block_index >= fs->sb.s_zmap_blocks) return;

    uint8_t buf[MINIX_BLOCK_SIZE];
    uint32_t block = zmap_start_block + block_index;

    if (read_fs(fs->block_device, block * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, buf) != MINIX_BLOCK_SIZE) return;

    buf[bit_offset / 8] &= ~(1 << (bit_offset % 8));

    write_fs(fs->block_device, block * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, buf);
}

static void minix_free_inode(minix_fs_t *fs, uint32_t inode_num) {
    if (inode_num == 0) return;
    minix_ncache_forget(fs, inode_num);

    uint32_t imap_start_block = 2;
    uint32_t block_index = inode_num / (MINIX_BLOCK_SIZE * 8);
    uint32_t bit_offset = inode_num % (MINIX_BLOCK_SIZE * 8);

    if (block_index >= fs->sb.s_imap_blocks) return;

    uint8_t buf[MINIX_BLOCK_SIZE];
    uint32_t block = imap_start_block + block_index;

    if (read_fs(fs->block_device, block * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, buf) != MINIX_BLOCK_SIZE) return;

    buf[bit_offset / 8] &= ~(1 << (bit_offset % 8));

    write_fs(fs->block_device, block * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, buf);
}

static void minix_free_all_zones(minix_fs_t *fs, fs_node_t *node) {
    if (fs->sb.s_magic == MINIX_V2_Magic || fs->sb.s_magic == MINIX_V2_Magic_14) {
         struct minix_inode_v2 *inode = (struct minix_inode_v2 *)node->ptr;
         // Free V2 zones
         for (int i=0; i<7; i++) {
             if (inode->i_zone[i]) {
                 minix_free_block(fs, inode->i_zone[i]);
                 inode->i_zone[i] = 0;
             }
         }
         // Indirect
         if (inode->i_zone[7]) {
             uint32_t *buf = (uint32_t *)kmalloc(MINIX_BLOCK_SIZE);
             if (buf) {
                 if (minix_read_zone(fs, inode->i_zone[7], (uint8_t*)buf) == MINIX_BLOCK_SIZE) {
                     for (int i=0; i<MINIX_BLOCK_SIZE/4; i++) {
                         if (buf[i]) minix_free_block(fs, buf[i]);
                     }
                 }
                 kfree(buf, MINIX_BLOCK_SIZE);
             }
             minix_free_block(fs, inode->i_zone[7]);
             inode->i_zone[7] = 0;
         }
         // Double Indirect
         if (inode->i_zone[8]) {
             uint32_t *buf = (uint32_t *)kmalloc(MINIX_BLOCK_SIZE);
             if (buf) {
                 if (minix_read_zone(fs, inode->i_zone[8], (uint8_t*)buf) == MINIX_BLOCK_SIZE) {
                     for (int i=0; i<MINIX_BLOCK_SIZE/4; i++) {
                         if (buf[i]) {
                             uint32_t *buf2 = (uint32_t *)kmalloc(MINIX_BLOCK_SIZE);
                             if (buf2) {
                                 if (minix_read_zone(fs, buf[i], (uint8_t*)buf2) == MINIX_BLOCK_SIZE) {
                                     for (int j=0; j<MINIX_BLOCK_SIZE/4; j++) {
                                         if (buf2[j]) minix_free_block(fs, buf2[j]);
                                     }
                                 }
                                 kfree(buf2, MINIX_BLOCK_SIZE);
                             }
                             minix_free_block(fs, buf[i]);
                         }
                     }
                 }
                 kfree(buf, MINIX_BLOCK_SIZE);
             }
             minix_free_block(fs, inode->i_zone[8]);
             inode->i_zone[8] = 0;
         }
         // Triple Indirect
         if (inode->i_zone[9]) {
             uint32_t *buf = (uint32_t *)kmalloc(MINIX_BLOCK_SIZE);
             if (buf) {
                 if (minix_read_zone(fs, inode->i_zone[9], (uint8_t*)buf) == MINIX_BLOCK_SIZE) {
                     for (int i=0; i<MINIX_BLOCK_SIZE/4; i++) {
                         if (buf[i]) {
                             uint32_t *buf2 = (uint32_t *)kmalloc(MINIX_BLOCK_SIZE);
                             if (buf2) {
                                 if (minix_read_zone(fs, buf[i], (uint8_t*)buf2) == MINIX_BLOCK_SIZE) {
                                     for (int j=0; j<MINIX_BLOCK_SIZE/4; j++) {
                                         if (buf2[j]) {
                                             uint32_t *buf3 = (uint32_t *)kmalloc(MINIX_BLOCK_SIZE);
                                             if (buf3) {
                                                 if (minix_read_zone(fs, buf2[j], (uint8_t*)buf3) == MINIX_BLOCK_SIZE) {
                                                     for (int k=0; k<MINIX_BLOCK_SIZE/4; k++) {
                                                         if (buf3[k]) minix_free_block(fs, buf3[k]);
                                                     }
                                                 }
                                                 kfree(buf3, MINIX_BLOCK_SIZE);
                                             }
                                             minix_free_block(fs, buf2[j]);
                                         }
                                     }
                                 }
                                 kfree(buf2, MINIX_BLOCK_SIZE);
                             }
                             minix_free_block(fs, buf[i]);
                         }
                     }
                 }
                 kfree(buf, MINIX_BLOCK_SIZE);
             }
             minix_free_block(fs, inode->i_zone[9]);
             inode->i_zone[9] = 0;
         }
    } else {
         struct minix_inode_v1 *inode = (struct minix_inode_v1 *)node->ptr;
         for (int i = 0; i < 7; i++) {
            if (inode->i_zone[i]) {
                minix_free_block(fs, inode->i_zone[i]);
                inode->i_zone[i] = 0;
            }
        }

        if (inode->i_zone[7]) {
            uint16_t *buf = (uint16_t *)kmalloc(MINIX_BLOCK_SIZE);
            if (buf) {
                if (minix_read_zone(fs, inode->i_zone[7], (uint8_t*)buf) == MINIX_BLOCK_SIZE) {
                    for (int i = 0; i < MINIX_BLOCK_SIZE / 2; i++) {
                        if (buf[i]) minix_free_block(fs, buf[i]);
                    }
                }
                kfree(buf, MINIX_BLOCK_SIZE);
            }
            minix_free_block(fs, inode->i_zone[7]);
            inode->i_zone[7] = 0;
        }

        if (inode->i_zone[8]) {
            uint16_t *buf = (uint16_t *)kmalloc(MINIX_BLOCK_SIZE);
            if (buf) {
                if (minix_read_zone(fs, inode->i_zone[8], (uint8_t*)buf) == MINIX_BLOCK_SIZE) {
                    for (int i = 0; i < MINIX_BLOCK_SIZE / 2; i++) {
                        if (buf[i]) {
                            uint16_t *buf2 = (uint16_t *)kmalloc(MINIX_BLOCK_SIZE);
                            if (buf2) {
                                if (minix_read_zone(fs, buf[i], (uint8_t*)buf2) == MINIX_BLOCK_SIZE) {
                                    for (int j = 0; j < MINIX_BLOCK_SIZE / 2; j++) {
                                        if (buf2[j]) minix_free_block(fs, buf2[j]);
                                    }
                                }
                                kfree(buf2, MINIX_BLOCK_SIZE);
                            }
                            minix_free_block(fs, buf[i]);
                        }
                    }
                }
                kfree(buf, MINIX_BLOCK_SIZE);
            }
            minix_free_block(fs, inode->i_zone[8]);
            inode->i_zone[8] = 0;
        }
    }
}

static size_t minix_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    minix_fs_t *fs = (minix_fs_t *)(uintptr_t)node->impl;
    /* A negative offset passed the >= length test and was then used as a
     * huge unsigned one. */
    if (offset < 0 || offset >= node->length) return 0;
    if ((off_t)size > node->length - offset) size = (size_t)(node->length - offset);

    size_t read_bytes = 0;
    uint32_t current_offset = (uint32_t)offset;
    
    while (read_bytes < size) {
        uint32_t block_index = current_offset / MINIX_BLOCK_SIZE;
        uint32_t block_offset = current_offset % MINIX_BLOCK_SIZE;
        uint32_t zone = minix_get_zone(fs, node, block_index);
        
        if (zone == 0) {
            // Sparse file (hole)
            memset(buffer + read_bytes, 0, 1); // Only 1 byte for simplicity, or chunk
            // Better: calculate chunk
            uint32_t chunk = MINIX_BLOCK_SIZE - block_offset;
            if (chunk > size - read_bytes) chunk = size - read_bytes;
            memset(buffer + read_bytes, 0, chunk);
            read_bytes += chunk;
            current_offset += chunk;
            continue;
        }

        uint8_t block_buf[MINIX_BLOCK_SIZE];
        if (read_fs(fs->block_device, minix_zone_off(zone), MINIX_BLOCK_SIZE, block_buf) != MINIX_BLOCK_SIZE) {
            break;
        }

        uint32_t copy_size = MINIX_BLOCK_SIZE - block_offset;
        if (copy_size > size - read_bytes) copy_size = size - read_bytes;
        
        memcpy(buffer + read_bytes, block_buf + block_offset, copy_size);
        read_bytes += copy_size;
        current_offset += copy_size;
    }
    return read_bytes;
}

/* Largest byte offset a MINIX file can name: zone numbers are 32-bit and a
 * zone is MINIX_BLOCK_SIZE bytes, but the on-disk i_size fields this driver
 * maintains are 32-bit, so that is the real ceiling. */
#define MINIX_MAX_FILE_OFF  0xFFFFFFFFu

static size_t minix_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    minix_fs_t *fs = (minix_fs_t *)(uintptr_t)node->impl;

    /*
     * There used to be no offset bound at all, and off_t (signed 64-bit)
     * was truncated straight into a uint32_t.  A negative offset from
     * lseek(fd, -1, SEEK_SET) became ~4 GiB, and any offset above 4 GiB
     * wrapped to a small one -- so a write intended past the end landed on
     * the START of the file, or, combined with an unchecked zone number,
     * on the superblock.
     */
    if (offset < 0) return 0;
    if ((uint64_t)offset > MINIX_MAX_FILE_OFF) return 0;
    if (size > MINIX_MAX_FILE_OFF - (uint64_t)offset) {
        size = (size_t)(MINIX_MAX_FILE_OFF - (uint64_t)offset);
    }

    size_t written = 0;
    uint32_t current_offset = (uint32_t)offset;

    while (written < size) {
        uint32_t block_index = current_offset / MINIX_BLOCK_SIZE;
        uint32_t block_offset = current_offset % MINIX_BLOCK_SIZE;
        uint32_t zone = 0;

        int ensure_rc = minix_ensure_zone(fs, node, block_index, &zone);
        if (ensure_rc < 0 || zone == 0) {
            break;
        }

        uint32_t chunk = MINIX_BLOCK_SIZE - block_offset;
        if (chunk > size - written) chunk = size - written;

        uint8_t block_buf[MINIX_BLOCK_SIZE];
        bool need_read = ((block_offset != 0) || (chunk != MINIX_BLOCK_SIZE)) && (ensure_rc == 0);

        if (need_read) {
             if (read_fs(fs->block_device, minix_zone_off(zone), MINIX_BLOCK_SIZE, block_buf) != MINIX_BLOCK_SIZE) {
                 break;
             }
        } else {
            memset(block_buf, 0, sizeof(block_buf));
        }

        memcpy(block_buf + block_offset, buffer + written, chunk);

        if (write_fs(fs->block_device, minix_zone_off(zone), MINIX_BLOCK_SIZE, block_buf) != MINIX_BLOCK_SIZE) {
            break;
        }

        written += chunk;
        current_offset += chunk;
    }
    
    // Update size if extended
    if (offset + (off_t)written > node->length) {
        node->length = offset + written;
        if (fs->sb.s_magic == MINIX_V2_Magic || fs->sb.s_magic == MINIX_V2_Magic_14) {
            ((struct minix_inode_v2 *)node->ptr)->i_size = (uint32_t)node->length;
        } else {
            ((struct minix_inode_v1 *)node->ptr)->i_size = (uint32_t)node->length;
        }
        minix_write_inode(fs, node);
    }
    return written;
}

static int minix_readlink(fs_node_t *node, char *buf, size_t size) {
    return (int)minix_read(node, 0, size, (uint8_t *)buf);
}

static struct dirent *minix_readdir(fs_node_t *node, uint64_t index) {
    minix_fs_t *fs = (minix_fs_t *)(uintptr_t)node->impl;
    if (!fs) return NULL;
    /* Stride and name length come from the on-disk variant. */
    uint32_t entry_size = minix_dirent_size(fs);
    uint32_t namelen    = minix_dirent_namelen(fs);
    uint32_t offset = 0;
    uint64_t seen = 0;
    struct minix_dirent_v1 entry;

    while (offset < node->length) {
        memset(&entry, 0, sizeof(entry));
        if (minix_read(node, offset, entry_size, (uint8_t *)&entry) != entry_size) {
            return NULL;
        }
        offset += entry_size;

        if (entry.inode == 0) {
            continue;
        }
        if (seen++ != index) {
            continue;
        }

        struct minix_inode_wrapper *wrapper = (struct minix_inode_wrapper *)node->ptr;
        strncpy(wrapper->dirent.d_name, entry.name, namelen);
        wrapper->dirent.d_name[namelen] = '\0';
        wrapper->dirent.d_ino = entry.inode;
        /* Entry-index readdir; 0 d_off => getdents +1 fallback. */
        wrapper->dirent.d_off = 0;
        return &wrapper->dirent;
    }

    return NULL;
}

/*
 * Scan the directory itself, into an entry of its own.  This went through
 * minix_readdir, which returns the directory node's one shared dirent: two
 * lookups in the same directory at once overwrote each other's name and
 * inode between the readdir and the compare, and a walk got another file
 * (or none).  It also rescanned from the start for every index.
 */
static fs_node_t *minix_finddir(fs_node_t *node, char *name) {
    minix_fs_t *fs = (minix_fs_t *)(uintptr_t)node->impl;
    if (!fs || !name) return NULL;
    uint32_t entry_size = minix_dirent_size(fs);
    uint32_t namelen    = minix_dirent_namelen(fs);
    size_t want = strlen(name);
    struct minix_dirent_v1 entry;
    uint32_t ino = 0;

    if (want == 0 || want > namelen) return NULL;
    for (uint32_t offset = 0; offset < node->length; offset += entry_size) {
        memset(&entry, 0, sizeof(entry));
        if (minix_read(node, offset, entry_size, (uint8_t *)&entry) != entry_size)
            return NULL;
        if (entry.inode == 0) continue;
        /* The name is NUL-padded, and unterminated when it fills the slot. */
        if (strncmp(entry.name, name, namelen) == 0 &&
            (want == namelen || entry.name[want] == '\0')) {
            ino = entry.inode;
            break;
        }
    }
    if (ino == 0) return NULL;

    /* Returned held, as a lookup's result is. */
    fs_node_t *hit = minix_ncache_find_held(fs, ino);
    if (hit) return hit;
    fs_node_t *result = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    if (result == NULL) {
        return NULL;
    }
    memset(result, 0, sizeof(fs_node_t));
    if (minix_read_inode(fs, ino, result) != 0) {
        kfree(result, sizeof(fs_node_t));
        return NULL;
    }
    return minix_ncache_insert(fs, result);
}

static fs_node_t *minix_mount(const char *device, uint32_t flags, void *data) {
    (void)device; (void)flags;
    fs_node_t *dev = (fs_node_t *)data;
    if (!dev) return NULL;

    minix_fs_t *fs = (minix_fs_t *)kmalloc(sizeof(minix_fs_t));
    if (!fs) return NULL;
    memset(fs, 0, sizeof(minix_fs_t));
    fs->block_device = dev;

    uint8_t buf[MINIX_BLOCK_SIZE];
    if (read_fs(dev, MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, buf) != MINIX_BLOCK_SIZE) {
        kfree(fs, sizeof(minix_fs_t));
        return NULL;
    }

    memcpy(&fs->sb, buf, sizeof(struct minix_superblock));

    /* Check Magic - Supports V1 (0x137F) mainly for now */
    if (fs->sb.s_magic != MINIX_V1_Magic && 
        fs->sb.s_magic != MINIX_V1_Magic_14 && 
        fs->sb.s_magic != MINIX_V2_Magic &&
        fs->sb.s_magic != MINIX_V2_Magic_14) {
        kprint("Minix: Invalid magic\n");
        kfree(fs, sizeof(minix_fs_t));
        return NULL;
    }
    /* This driver takes a zone to be one 1 KiB block throughout; a volume
     * with larger zones would be read and written wrongly (and statfs
     * shifted by whatever the superblock said). */
    if (fs->sb.s_log_zone_size != 0) {
        kprint("Minix: zones larger than a block are not supported\n");
        kfree(fs, sizeof(minix_fs_t));
        return NULL;
    }

    kprint("Minix: Filesystem detected.\n");

    fs_node_t *root = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    if (!root) {
        kfree(fs, sizeof(minix_fs_t));
        return NULL;
    }
    memset(root, 0, sizeof(fs_node_t));

    // Read inode 1 (Root)
    if (minix_read_inode(fs, MINIX_ROOT_INODE, root) != 0) {
        kprint("Minix: Failed to read root inode\n");
        kfree(root, sizeof(fs_node_t));
        kfree(fs, sizeof(minix_fs_t));
        return NULL;
    }

    snprintf(root->name, sizeof(root->name), "minix_root"); // Usually VFS overrides name with mountpoint
    root->unmount = minix_unmount;
    /* Unmount frees the root; its last close must not. */
    ((struct minix_inode_wrapper *)root->ptr)->is_root = 1;
    return root;
}

static int minix_unmount(fs_node_t *root) {
    if (!root) return -EINVAL;
    minix_fs_t *fs = (minix_fs_t *)(uintptr_t)root->impl;

    /* Cached nodes of this volume go with it: detach them onto a chain of
     * their own under the lock, free them after. */
    fs_node_t *dead = NULL;
    spinlock_acquire(&minix_ncache_lock);
    for (int b = 0; b < MINIX_NCACHE_BUCKETS; b++) {
        fs_node_t **pp = &minix_ncache[b];
        while (*pp) {
            fs_node_t *n = *pp;
            if (n->impl == (uintptr_t)fs) {
                *pp = minix_w(n)->ncache_next;
                minix_w(n)->ncache_next = dead;
                dead = n;
            } else {
                pp = &minix_w(n)->ncache_next;
            }
        }
    }
    spinlock_release(&minix_ncache_lock);
    while (dead) {
        fs_node_t *next = minix_w(dead)->ncache_next;
        minix_node_destroy(dead);
        dead = next;
    }

    if (root->ptr) {
        kfree(root->ptr, sizeof(struct minix_inode_wrapper));
    }

    if (fs) {
        kfree(fs, sizeof(minix_fs_t));
    }

    kfree(root, sizeof(fs_node_t));
    return 0;
}

static int minix_alloc_inode(minix_fs_t *fs) {
    uint32_t total_inodes = fs->sb.s_ninodes;
    uint32_t imap_blocks = fs->sb.s_imap_blocks;
    uint32_t start_block = 2; // Boot + Super

    for (uint32_t b = 0; b < imap_blocks; b++) {
        uint8_t buf[MINIX_BLOCK_SIZE];
        if (read_fs(fs->block_device, (start_block + b) * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, buf) != MINIX_BLOCK_SIZE) {
            return -1;
        }

        for (uint32_t i = 0; i < MINIX_BLOCK_SIZE; i++) {
            if (buf[i] != 0xFF) {
                for (int j = 0; j < 8; j++) {
                    if (!((buf[i] >> j) & 1)) {
                        uint32_t inode = b * 8192 + i * 8 + j;
                        if (inode < 1 || inode > total_inodes) continue;

                        buf[i] |= (1 << j);
                        if (write_fs(fs->block_device, (start_block + b) * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, buf) != MINIX_BLOCK_SIZE) {
                            return -1;
                        }
                        return inode;
                    }
                }
            }
        }
    }
    return -1;
}


static int minix_dir_add(fs_node_t *dir, const char *name, uint32_t inode_num) {
    minix_fs_t *fs = (minix_fs_t *)(uintptr_t)dir->impl;
    /* Entries are 16 bytes (14-char names) or 32 (30-char), by the
     * volume's magic.  A fixed 32-byte entry on a 14-char volume wrote over
     * two slots at once and misaligned every entry after it. */
    uint32_t ent_size = minix_dirent_size(fs);
    uint32_t name_max = minix_dirent_namelen(fs);
    uint8_t entry[32], tmp[32];

    memset(entry, 0, sizeof(entry));
    uint16_t ino16 = (uint16_t)inode_num;
    memcpy(entry, &ino16, sizeof(ino16));
    size_t nlen = strlen(name);
    memcpy(entry + 2, name, nlen < name_max ? nlen : name_max);

    uint32_t offset = 0;

    while (offset < dir->length) {
        if (minix_read(dir, offset, ent_size, tmp) != ent_size) break;
        uint16_t used;
        memcpy(&used, tmp, sizeof(used));
        if (used == 0) {
            // Found empty slot
            if (minix_write(dir, offset, ent_size, entry) != ent_size) return -1;
            return 0;
        }
        offset += ent_size;
    }

    // Append
    if (minix_write(dir, offset, ent_size, entry) != ent_size) {
        return -1;
    }

    /* Update the directory's on-disk size.
     *
     * This used to cast dir->ptr to minix_inode_v1 * and call
     * minix_write_inode_raw(), which hardcodes 32-byte inodes and 32 per
     * block.  On a MINIX V2 filesystem -- which minix_mount() accepts --
     * inodes are 64 bytes, 16 per block, so the write landed in the wrong
     * block at the wrong offset, on top of an unrelated inode's
     * mode/links/uid/gid/size.  Setting cached->i_size at V1 offset 4 also
     * wrote over V2's i_uid/i_gid.
     *
     * minix_write_inode() is version-aware and already takes the size from
     * node->length, so it is both correct and the smaller change. */
    if (dir->ptr) {
        minix_write_inode(fs, dir);
    }

    return 0;
}

static int minix_mknod(fs_node_t *dir, const char *name, uint16_t mode, uint32_t dev) {
    minix_fs_t *fs = (minix_fs_t *)(uintptr_t)dir->impl;

    // 1. Allocate Inode
    int inode_num = minix_alloc_inode(fs);
    if (inode_num < 0) return -ENOSPC;

    // 2. Prepare FS Node wrapper
    fs_node_t new_node;
    memset(&new_node, 0, sizeof(fs_node_t));
    new_node.inode = (uint64_t)inode_num;
    new_node.impl = (uintptr_t)fs;

    // Set type flags
    if (S_ISDIR(mode)) new_node.flags = FS_DIRECTORY;
    else if (S_ISCHR(mode)) new_node.flags = FS_CHARDEVICE;
    else if (S_ISBLK(mode)) new_node.flags = FS_BLOCKDEVICE;
    else if (S_ISREG(mode)) new_node.flags = FS_FILE;
    else if (S_ISFIFO(mode)) new_node.flags = FS_PIPE;
    else if (S_ISLNK(mode)) new_node.flags = FS_SYMLINK;
    else new_node.flags = 0;

    new_node.mask = mode & 0xFFF;
    new_node.uid = dir->uid;
    new_node.gid = dir->gid;
    new_node.length = 0;

    // 3. Allocate and Initialize Cached Raw Inode
    bool v2 = (fs->sb.s_magic == MINIX_V2_Magic || fs->sb.s_magic == MINIX_V2_Magic_14);

    void *raw_inode = kmalloc(sizeof(struct minix_inode_wrapper));
    if (!raw_inode) {
        minix_free_inode(fs, (uint32_t)inode_num);
        return -EIO;
    }
    memset(raw_inode, 0, sizeof(struct minix_inode_wrapper));

    // Initialize specific fields in raw inode that minix_write_inode preserves
    if (v2) {
        struct minix_inode_v2 *v2_inode = (struct minix_inode_v2 *)raw_inode;
        v2_inode->i_nlinks = 1;
        v2_inode->i_zone[0] = dev;
    } else {
        struct minix_inode_v1 *v1_inode = (struct minix_inode_v1 *)raw_inode;
        v1_inode->i_nlinks = 1;
        v1_inode->i_zone[0] = (uint16_t)dev;
    }

    new_node.ptr = (struct fs_node *)raw_inode;

    // 4. Write Inode to Disk (using V1/V2 aware helper)
    if (minix_write_inode(fs, &new_node) != 0) {
        kfree(raw_inode, sizeof(struct minix_inode_wrapper));
        minix_free_inode(fs, (uint32_t)inode_num);
        return -EIO;
    }

    kfree(raw_inode, sizeof(struct minix_inode_wrapper));

    // 5. Add to Directory
    // 5. Add to Directory
    if (minix_dir_add(dir, name, inode_num) != 0) {
        minix_free_inode(fs, (uint32_t)inode_num);
        return -EIO;
    }

    return 0;
}

/* The link count in a cached raw inode, which sits at a different offset in
 * the V1 and V2 layouts (see minix_rmdir). */
static void minix_nlinks_add(minix_fs_t *fs, void *raw, int delta) {
    if (fs->sb.s_magic == MINIX_V2_Magic || fs->sb.s_magic == MINIX_V2_Magic_14) {
        struct minix_inode_v2 *ri = (struct minix_inode_v2 *)raw;
        ri->i_nlinks = (uint16_t)(ri->i_nlinks + delta);
    } else {
        struct minix_inode_v1 *ri = (struct minix_inode_v1 *)raw;
        ri->i_nlinks = (uint8_t)(ri->i_nlinks + delta);
    }
}

/*
 * mkdir(2).  The driver had none -- every mkdir on a Minix volume was
 * EOPNOTSUPP -- and minix_mknod with an S_IFDIR mode is no substitute: it
 * makes an inode with one link and no "." or "..".  A directory gets two
 * links (its entry and its "."), both entries, and its parent one more link
 * for the "..".
 */
static int minix_mkdir(fs_node_t *dir, const char *name, uint16_t permission) {
    minix_fs_t *fs = (minix_fs_t *)(uintptr_t)dir->impl;

    if (!name || !name[0]) return -EINVAL;
    if (!strcmp(name, ".") || !strcmp(name, "..")) return -EEXIST;
    if (strlen(name) > minix_dirent_namelen(fs)) return -ENAMETOOLONG;

    int inode_num = minix_alloc_inode(fs);
    if (inode_num < 0) return -ENOSPC;

    fs_node_t nd;
    memset(&nd, 0, sizeof(nd));
    nd.inode = (uint64_t)inode_num;
    nd.impl = (uintptr_t)fs;
    nd.flags = FS_DIRECTORY;
    nd.mask = permission & 07777;
    nd.uid = dir->uid;
    nd.gid = dir->gid;
    nd.length = 0;
    nd.ptr = kmalloc(sizeof(struct minix_inode_wrapper));
    if (!nd.ptr) {
        minix_free_inode(fs, (uint32_t)inode_num);
        return -ENOMEM;
    }
    memset(nd.ptr, 0, sizeof(struct minix_inode_wrapper));
    minix_nlinks_add(fs, nd.ptr, 2);

    /* The entries first (each write extends the directory and commits its
     * inode), then the name in the parent, then the parent's "..". */
    if (minix_write_inode(fs, &nd) != 0 ||
        minix_dir_add(&nd, ".", (uint32_t)inode_num) != 0 ||
        minix_dir_add(&nd, "..", (uint32_t)dir->inode) != 0 ||
        minix_dir_add(dir, name, (uint32_t)inode_num) != 0) {
        kfree(nd.ptr, sizeof(struct minix_inode_wrapper));
        minix_free_inode(fs, (uint32_t)inode_num);
        return -EIO;
    }
    kfree(nd.ptr, sizeof(struct minix_inode_wrapper));

    if (dir->ptr) {
        minix_nlinks_add(fs, dir->ptr, 1);
        minix_write_inode(fs, dir);
    }
    return 0;
}

/* (dir, target, name), as symlink_type_t says and symlink_fs passes: the
 * parameters were declared the other way round, so every link was made
 * with its target as its name and its name as its target. */
static int minix_symlink(fs_node_t *dir, const char *target, const char *name) {
    minix_fs_t *fs = (minix_fs_t *)(uintptr_t)dir->impl;

    // 1. Allocate Inode
    int inode_num_int = minix_alloc_inode(fs);
    if (inode_num_int < 0) return -ENOSPC;
    uint32_t inode_num = (uint32_t)inode_num_int;

    // 2. Initialize Inode
    fs_node_t new_node;
    memset(&new_node, 0, sizeof(fs_node_t));
    new_node.inode = inode_num;
    new_node.impl = (uintptr_t)fs;
    new_node.flags = FS_SYMLINK;
    new_node.mask = 0777;
    new_node.uid = dir->uid;
    new_node.gid = dir->gid;
    new_node.length = 0;

    bool v2 = (fs->sb.s_magic == MINIX_V2_Magic || fs->sb.s_magic == MINIX_V2_Magic_14);

    new_node.ptr = kmalloc(sizeof(struct minix_inode_wrapper));
    if (!new_node.ptr) {
        minix_free_inode(fs, inode_num);
        return -EIO;
    }
    memset(new_node.ptr, 0, sizeof(struct minix_inode_wrapper));
    if (v2) ((struct minix_inode_v2 *)new_node.ptr)->i_nlinks = 1;
    else ((struct minix_inode_v1 *)new_node.ptr)->i_nlinks = 1;

    // 3. Allocate Zone and Write Target
    uint32_t zone = minix_alloc_zone(fs);
    if (zone == 0) {
        kfree(new_node.ptr, sizeof(struct minix_inode_wrapper));
        minix_free_inode(fs, inode_num);
        return -EIO;
    }

    if (v2) ((struct minix_inode_v2 *)new_node.ptr)->i_zone[0] = zone;
    else ((struct minix_inode_v1 *)new_node.ptr)->i_zone[0] = zone;

    size_t target_len = strlen(target);
    // Note: minix_write handles updating new_node.length but we need to ensure it uses the correct zone we just set.
    // minix_write uses minix_get_zone which uses new_node.ptr. So it should work.

    if (minix_write(&new_node, 0, target_len, (uint8_t *)target) != target_len) {
        kfree(new_node.ptr, sizeof(struct minix_inode_wrapper));
        minix_free_block(fs, zone);
        minix_free_inode(fs, inode_num);
        return -EIO;
    }

    if (minix_write_inode(fs, &new_node) != 0) {
        kfree(new_node.ptr, sizeof(struct minix_inode_wrapper));
        minix_free_block(fs, zone);
        minix_free_inode(fs, inode_num);
        return -EIO;
    }

    kfree(new_node.ptr, sizeof(struct minix_inode_wrapper));

    // 4. Add to directory
    if (minix_dir_add(dir, name, (uint16_t)inode_num) != 0) {
        minix_free_block(fs, zone);
        minix_free_inode(fs, inode_num);
        return -EIO;
    }

    return 0;
}

static int minix_link(fs_node_t *dir, fs_node_t *node, const char *name) {
    if (!dir || !node || !name) return -EINVAL;
    if (strlen(name) > 30) return -ENAMETOOLONG;

    minix_fs_t *fs = (minix_fs_t *)(uintptr_t)dir->impl;

    struct minix_dirent_v1 {
        uint16_t inode;
        char name[30];
    } __attribute__((packed));

    // 1. Find a free directory entry
    uint32_t offset = 0;
    uint32_t dir_size = dir->length;
    uint32_t free_offset = 0;
    bool found = false;

    /* Stride from the on-disk variant, not a hardcoded 32. */
    uint32_t ent_size = minix_dirent_size(fs);
    uint32_t namelen  = minix_dirent_namelen(fs);
    uint8_t buf[sizeof(struct minix_dirent_v1)];

    /*
     * This loop used to stop at the first free slot without ever
     * comparing names, so creating a name that already existed appended a
     * SECOND entry for it.  Which one a later lookup found then depended on
     * scan order, and unlink removed only one of them.  Scan the whole
     * directory: reject a duplicate, and remember the first free slot.
     */
    while (offset < dir_size) {
        memset(buf, 0, sizeof(buf));
        if (minix_read(dir, offset, ent_size, buf) != ent_size) {
            break;
        }
        struct minix_dirent_v1 *d = (struct minix_dirent_v1 *)buf;
        if (d->inode == 0) {
            if (!found) { free_offset = offset; found = true; }
        } else {
            char have[31];
            strncpy(have, d->name, namelen);
            have[namelen] = '\0';
            if (strcmp(have, name) == 0) {
                return -EEXIST;              /* name already present */
            }
        }
        offset += ent_size;
    }

    if (!found) {
        // Append to directory
        free_offset = dir_size;
    }

    // 2. Increment link count
    bool v2 = (fs->sb.s_magic == MINIX_V2_Magic || fs->sb.s_magic == MINIX_V2_Magic_14);
    if (!node->ptr) return -EIO;

    if (v2) {
        struct minix_inode_v2 *inode = (struct minix_inode_v2 *)node->ptr;
        if (inode->i_nlinks == 0xFFFFU) return -EMLINK;
        inode->i_nlinks++;
        if (minix_write_inode(fs, node) != 0) {
            inode->i_nlinks--;
            return -EIO;
        }
    } else {
        struct minix_inode_v1 *inode = (struct minix_inode_v1 *)node->ptr;
        if (inode->i_nlinks == 255U) return -EMLINK;
        inode->i_nlinks++;
        if (minix_write_inode(fs, node) != 0) {
            inode->i_nlinks--;
            return -EIO;
        }
    }

    // 3. Write directory entry
    struct minix_dirent_v1 entry;
    memset(&entry, 0, sizeof(entry));
    entry.inode = node->inode;
    strncpy(entry.name, name, namelen);

    if (minix_write(dir, free_offset, ent_size, (uint8_t *)&entry) != ent_size) {
        // Rollback
        if (v2) {
            ((struct minix_inode_v2 *)node->ptr)->i_nlinks--;
        } else {
            ((struct minix_inode_v1 *)node->ptr)->i_nlinks--;
        }
        minix_write_inode(fs, node);
        return -EIO;
    }

    // Update mtime/ctime
    dir->mtime = get_time();
    dir->ctime = dir->mtime;

    /* Persist changes.  minix_write_inode takes the size from dir->length
     * and knows both inode layouts; setting i_size through a V1 cast here
     * wrote over a V2 inode's uid and gid. */
    minix_write_inode(fs, dir);


    return 0;
}

/*
 * Remove `name` from `dir`.  unlink(2) refuses a directory (EISDIR);
 * rmdir(2) removes only a directory, and drops both of its links -- its
 * entry and its own "." -- so the inode and its zones are freed.  rmdir
 * used to call unlink, which refused every directory, so it always failed.
 */
static int minix_remove_entry(fs_node_t *dir, const char *name, int is_rmdir);

static int minix_unlink(fs_node_t *dir, const char *name) {
    return minix_remove_entry(dir, name, 0);
}

static int minix_remove_entry(fs_node_t *dir, const char *name, int is_rmdir) {
    minix_fs_t *fs = (minix_fs_t *)(uintptr_t)dir->impl;
    struct minix_dirent_v1 {
        uint16_t inode;
        char name[30];
    } __attribute__((packed)) entry;

    uint32_t offset = 0;
    bool v2 = (fs->sb.s_magic == MINIX_V2_Magic || fs->sb.s_magic == MINIX_V2_Magic_14);
    /* Stride and name length from the on-disk variant. */
    uint32_t ent_size = minix_dirent_size(fs);
    uint32_t namelen  = minix_dirent_namelen(fs);

    while (offset < dir->length) {
        memset(&entry, 0, sizeof(entry));
        if (minix_read(dir, offset, ent_size, (uint8_t *)&entry) != ent_size) {
            return -EIO;
        }

        if (entry.inode != 0 && strncmp(entry.name, name, namelen) == 0) {
            uint32_t target_inode_num = entry.inode;

            // Read target inode FIRST
            fs_node_t target;
            memset(&target, 0, sizeof(target));
            if (minix_read_inode(fs, target_inode_num, &target) != 0) {
                return -EIO;
            }

            if (!target.ptr) {
                return -EIO;
            }

            /*
             * Unlink used to never check the target's type, so
             * unlink("somedir") removed the directory entry and dropped the
             * link count -- orphaning the whole subtree beneath it, with its
             * inodes and zones still marked in use and no name to reach them.
             * POSIX reserves directory removal for rmdir(2); unlink must
             * refuse with EISDIR.
             */
            int is_dir = (target.flags & 0x7) == FS_DIRECTORY;
            if (is_dir != is_rmdir) {
                kfree(target.ptr, sizeof(struct minix_inode_wrapper));
                return is_dir ? -EISDIR : -ENOTDIR;
            }

            // Remove directory entry
            entry.inode = 0;
            memset(entry.name, 0, sizeof(entry.name));
            if (minix_write(dir, offset, ent_size, (uint8_t *)&entry) != ent_size) {
                kfree(target.ptr, sizeof(struct minix_inode_wrapper));
                return -EIO;
            }

            if (v2) {
                struct minix_inode_v2 *t_inode = (struct minix_inode_v2 *)target.ptr;
                if (is_dir) t_inode->i_nlinks = 0;      /* entry and "." */
                else if (t_inode->i_nlinks > 0) t_inode->i_nlinks--;

                if (t_inode->i_nlinks == 0) {
                    minix_free_all_zones(fs, &target);
                    minix_free_inode(fs, target_inode_num);
                } else {
                    t_inode->i_ctime = get_time();
                    minix_write_inode(fs, &target);
                }
            } else {
                struct minix_inode_v1 *t_inode = (struct minix_inode_v1 *)target.ptr;
                if (is_dir) t_inode->i_nlinks = 0;      /* entry and "." */
                else if (t_inode->i_nlinks > 0) t_inode->i_nlinks--;

                if (t_inode->i_nlinks == 0) {
                    minix_free_all_zones(fs, &target);
                    minix_free_inode(fs, target_inode_num);
                } else {
                    t_inode->i_time = (uint32_t)get_time();
                    minix_write_inode(fs, &target);
                }
            }

            kfree(target.ptr, sizeof(struct minix_inode_wrapper));

            // Update parent directory timestamps
            dir->mtime = get_time();
            dir->ctime = dir->mtime;
            minix_write_inode(fs, dir);

            return 0;
        }
        offset += ent_size;          /* per-variant stride */
    }

    return -ENOENT;
}

static int minix_dir_is_empty(fs_node_t *node) {
    uint64_t idx = 0;
    struct dirent *de;

    if (!node || (node->flags & 0x7) != FS_DIRECTORY) return 0;

    while ((de = minix_readdir(node, idx++)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
            continue;
        }
        return 0;
    }

    return 1;
}

static int minix_rmdir(fs_node_t *dir, const char *name) {
    minix_fs_t *fs;
    fs_node_t *target;
    int ret;

    if (!dir || !name || !name[0]) return -EINVAL;
    if ((dir->flags & 0x7) != FS_DIRECTORY) return -ENOTDIR;
    if (!strcmp(name, ".") || !strcmp(name, "..")) return -EINVAL;

    /* Held by minix_finddir: removing the entry frees the inode, which
     * drops its cached node -- this one.  The close at each exit lets it
     * go. */
    target = minix_finddir(dir, (char *)name);
    if (!target) return -ENOENT;
    if ((target->flags & 0x7) != FS_DIRECTORY) {
        minix_node_close(target);
        return -ENOTDIR;
    }
    if (!minix_dir_is_empty(target)) {
        minix_node_close(target);
        return -ENOTEMPTY;
    }

    fs = (minix_fs_t *)(uintptr_t)dir->impl;
    ret = minix_remove_entry(dir, name, 1);
    if (ret != 0) {
        minix_node_close(target);
        return ret;
    }

    /* Drop the parent's link count for the removed subdirectory's "..".
     *
     * i_nlinks sits at a DIFFERENT offset in the two inode layouts (V1 byte 9,
     * V2 byte 2), so reading it through a minix_inode_v1 * on a V2 filesystem
     * decremented a byte of i_size instead -- masked immediately afterwards by
     * minix_write_inode() rewriting i_size from node->length, so the real link
     * count was never decremented and fsck reported drifting counts on every
     * rmdir. */
    if (dir->ptr) {
        if (fs->sb.s_magic == MINIX_V2_Magic ||
            fs->sb.s_magic == MINIX_V2_Magic_14) {
            struct minix_inode_v2 *di = (struct minix_inode_v2 *)dir->ptr;
            if (di->i_nlinks > 0) di->i_nlinks--;
        } else {
            struct minix_inode_v1 *di = (struct minix_inode_v1 *)dir->ptr;
            if (di->i_nlinks > 0) di->i_nlinks--;
        }
        minix_write_inode(fs, dir);
    }

    minix_node_close(target);
    return 0;
}

static int minix_write_inode(minix_fs_t *fs, fs_node_t *node) {
    if (!fs || !node || !node->ptr) return -1;

    uint16_t type_bits = minix_type_bits_from_flags(node->flags);

    if (fs->sb.s_magic == MINIX_V2_Magic || fs->sb.s_magic == MINIX_V2_Magic_14) {
        struct minix_inode_v2 *inode = (struct minix_inode_v2 *)node->ptr;
        if (type_bits) {
            inode->i_mode = (uint16_t)(type_bits | (node->mask & 0x0FFF));
        } else {
            inode->i_mode = (uint16_t)((inode->i_mode & 0xF000) | (node->mask & 0x0FFF));
        }
        inode->i_uid = (uint16_t)node->uid;
        inode->i_gid = (uint16_t)node->gid;
        inode->i_size = (uint32_t)node->length;
        inode->i_atime = (uint32_t)node->atime;
        inode->i_mtime = (uint32_t)node->mtime;
        inode->i_ctime = (uint32_t)node->ctime;

        // V2
        uint32_t inode_start = 2 + fs->sb.s_imap_blocks + fs->sb.s_zmap_blocks;
        uint32_t inodes_per_block = MINIX_BLOCK_SIZE / sizeof(struct minix_inode_v2);
        uint32_t block = inode_start + (node->inode - 1) / inodes_per_block;
        uint32_t offset = ((node->inode - 1) % inodes_per_block) * sizeof(struct minix_inode_v2);
        
        uint8_t buf[MINIX_BLOCK_SIZE];
        if (read_fs(fs->block_device, block * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, buf) != MINIX_BLOCK_SIZE) return -1;
        
        memcpy(buf + offset, node->ptr, sizeof(struct minix_inode_v2));
        
        if (write_fs(fs->block_device, block * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, buf) != MINIX_BLOCK_SIZE) return -1;
        return 0;
    } else {
        struct minix_inode_v1 *inode = (struct minix_inode_v1 *)node->ptr;
        if (type_bits) {
            inode->i_mode = (uint16_t)(type_bits | (node->mask & 0x0FFF));
        } else {
            inode->i_mode = (uint16_t)((inode->i_mode & 0xF000) | (node->mask & 0x0FFF));
        }
        inode->i_uid = (uint16_t)node->uid;
        inode->i_gid = (uint8_t)node->gid;
        inode->i_size = (uint32_t)node->length;
        inode->i_time = (uint32_t)node->mtime;

        // V1
        return minix_write_inode_raw(fs, node->inode, (struct minix_inode_v1 *)node->ptr);
    }
}

static int minix_write_inode_raw(minix_fs_t *fs, uint32_t inode_num, struct minix_inode_v1 *inode) {
    uint32_t inode_start_block = 2 + fs->sb.s_imap_blocks + fs->sb.s_zmap_blocks;
    uint32_t inodes_per_block = MINIX_BLOCK_SIZE / sizeof(struct minix_inode_v1);
    uint32_t block = inode_start_block + (inode_num - 1) / inodes_per_block;
    uint32_t offset = ((inode_num - 1) % inodes_per_block) * sizeof(struct minix_inode_v1);

    uint8_t buf[MINIX_BLOCK_SIZE];
    if (read_fs(fs->block_device, block * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, buf) != MINIX_BLOCK_SIZE) {
        return -1;
    }

    memcpy(buf + offset, inode, sizeof(struct minix_inode_v1));

    if (write_fs(fs->block_device, block * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, buf) != MINIX_BLOCK_SIZE) {
        return -1;
    }
    return 0;
}
