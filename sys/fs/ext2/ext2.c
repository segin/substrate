#include <string.h>

#include <crc32c.h>
#include <drivers/storage/blkdev.h>
#include <fs/ext2/ext2.h>
#include <kern/cmdline.h>
#include <kern/console.h>
#include <kern/time.h>
#include <sys/errno.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <vfs/vfs.h>
#include <vm/uma.h>
#include <vm/vm_kmem.h>

/*
 * ext2trace — gated on `debug=ext2trace` on the kernel command line.
 * Aims to catch the "/bin dirent points at wtmp inode" corruption:
 * every inode alloc, free, mode-type transition, and dirent
 * add/remove against an interesting parent (root + immediate
 * children of root) prints a kprintf line.  Cheap when off (one
 * cmdline lookup per call), loud when on.
 */
#define EXT2_TRACE_PARENT_LIMIT 64u   /* only log dirent ops on inodes <= this — keeps the channel readable */
static inline int ext2_trace_on(void) { return cmdline_debug_enabled("ext2trace"); }
static const char *ext2_trace_iftype(uint16_t mode) {
    switch (mode & S_IFMT) {
    case S_IFREG: return "REG";
    case S_IFDIR: return "DIR";
    case S_IFLNK: return "LNK";
    case S_IFCHR: return "CHR";
    case S_IFBLK: return "BLK";
    case S_IFIFO: return "FIFO";
    case S_IFSOCK: return "SOCK";
    case 0: return "ZERO";
    default: return "???";
    }
}

/* Cache for in-memory inodes.
 *
 * Each slot pins one inode for as long as any FD on the system holds it
 * open (pin_count > 0).  Real workloads pin a lot of slots transiently:
 * a single gcc compile drags in cc1 + as + ld + libc.so + libgcc.a + a
 * couple dozen header includes, and configure runs that gcc invocation
 * inside a shell that's itself holding script-source and heredoc fds.
 * 64 used to be enough for the in-tree test suite; configure exhausts
 * it (alloc_node fail with pinned=63 of 64) on the first conftest run.
 * Each cache slot is ~2 KB so 256 entries is ~512 KB static, fine on
 * the 128 MB QEMU configuration. */
#define EXT2_NODE_CACHE_SIZE 256
static ext2_node_t ext2_node_cache[EXT2_NODE_CACHE_SIZE];
static fs_node_t ext2_fs_node_cache[EXT2_NODE_CACHE_SIZE];
static int ext2_node_cache_idx = 0;

/*
 * Two pieces of ext2 shared state were mutated with no serialization at all
 * (both are process-context only — never touched from an ISR — so a plain
 * mutex is the right tool; initialised once in ext2_init()):
 *
 *   ext2_node_cache_lock (FS-01) — guards node-cache slot selection AND
 *     population in ext2_alloc_node().  Without it two concurrent lookups
 *     could both pick the same pin_count==0 slot and memcpy() two different
 *     on-disk inodes into it, silently handing one file's fs_node_t back
 *     as another's (the root of the reproduced live-symlink double-alloc).
 *
 *   ext2_inode_table_lock (FS-02) — serializes the read-modify-write of a
 *     whole inode-table block in ext2_write_inode().  Many inodes share one
 *     table block; two unserialized RMWs race and one writer's stale copy of
 *     a co-resident inode clobbers the other's just-written i_links_count.
 */
static mutex_t ext2_node_cache_lock;
static mutex_t ext2_inode_table_lock;

/* Forward decls — ext2_node_close needs these to complete a deferred
 * unlink (set up by ext2_unlink when pin_count > 0).  Their full
 * definitions live below alongside the rest of the inode allocator. */
static int ext2_free_inode_blocks(ext2_fs_t *fs, ext2_inode_t *inode);

/*
 * EXT2-18: pin_count is what stops ext2_alloc_node() from recycling a slot
 * out from under a live fs_node, but it was a plain uint16_t incremented and
 * decremented with no lock while the allocator scanned it under
 * ext2_node_cache_lock.  Two hazards: a lost update between concurrent
 * open()s (read-modify-write on a non-atomic field) undercounts the pin and
 * lets the slot be recycled while it is still open, and a decrement to 0
 * racing the allocator's scan lets the allocator claim a slot the closer is
 * still working on.  Both mutations now happen under the same lock the
 * allocator uses, so the scan sees a stable count.
 */
static void ext2_node_open(fs_node_t *node) {
    if (!node) return;
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    if (!ctx) return;
    mutex_lock(&ext2_node_cache_lock);
    ctx->pin_count++;
    mutex_unlock(&ext2_node_cache_lock);
}

static void ext2_node_close(fs_node_t *node) {
    if (!node) return;
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    if (!ctx) return;

    mutex_lock(&ext2_node_cache_lock);
    if (ctx->pin_count > 0) {
        ctx->pin_count--;
    }
    int finish_delete = (ctx->pin_count == 0 && ctx->orphaned && ctx->fs);
    if (finish_delete) {
        /* Re-pin across the teardown.  The block/inode frees below take
         * fs->alloc_lock and do I/O, so they must not run holding the cache
         * lock -- but a slot at pin_count 0 is fair game for the allocator,
         * which would hand this half-torn-down slot to another lookup.  The
         * pin keeps it reserved; it is dropped again at the end. */
        ctx->pin_count = 1;
    }
    mutex_unlock(&ext2_node_cache_lock);

    /* Deferred unlink: ext2_unlink saw open FDs and set ctx->orphaned
     * instead of freeing the inode + data blocks.  Now that the last
     * FD has closed, complete the delete the unlink path skipped. */
    if (finish_delete) {
        (void)ext2_free_inode_blocks(ctx->fs, &ctx->inode);
        ext2_free_inode(ctx->fs, ctx->inode_num,
                        ctx->was_dir_at_unlink ? 1 : 0);
        memset(&ctx->inode, 0, sizeof(ctx->inode));
        node->length = 0;
        ctx->orphaned = 0;
        ctx->was_dir_at_unlink = 0;
        /* Invalidate the cache slot.  ext2_alloc_node looks up by
         * (fs, inode_num); if we leave those set, the next caller
         * that asks for the now-freed inode_num (ext2_alloc_inode
         * hands recently-freed inodes back out fast — mkstemp loops
         * trigger this almost immediately) gets this stale slot
         * with the zeroed inode, instead of a freshly populated one.
         * Clearing fs/inode_num forces the alloc path to recycle
         * this slot and copy in the on-disk inode that ext2_finddir
         * just read.  */
        mutex_lock(&ext2_node_cache_lock);
        ctx->fs = NULL;
        ctx->inode_num = 0;
        ctx->pin_count = 0;     /* release the teardown re-pin */
        mutex_unlock(&ext2_node_cache_lock);
    }
}

/* Refuse a write op when the mount is read-only.  Returns the
 * supplied errno from the caller; used as:
 *     if (EXT2_RO_REFUSE(ctx->fs)) return -EROFS;
 * Stored on ext2_fs_t at mount time via MNT_RDONLY or auto-set
 * when unsupported ROCOMPAT bits were in the superblock.  */
#define EXT2_RO_REFUSE(fs)   ((fs) && (fs)->readonly)

// Forward declarations
fs_node_t *ext2_mount(const char *device, uint32_t flags, void *data);
int ext2_unmount(fs_node_t *node);
int ext2_remount(fs_node_t *node, uint32_t flags);
size_t ext2_file_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer);
size_t ext2_file_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer);
struct dirent *ext2_readdir(fs_node_t *node, uint64_t index);
fs_node_t *ext2_finddir(fs_node_t *node, char *name);
int ext2_readlink(fs_node_t *node, char *buf, size_t size);
uint32_t ext2_alloc_block(ext2_fs_t *fs);
static int ext2_mknod(fs_node_t *dir, const char *name, uint16_t mode, uint32_t dev);
int ext2_mkdir(fs_node_t *dir, const char *name, uint16_t permission);
static int ext2_symlink(fs_node_t *dir, const char *target, const char *name);
int ext2_unlink(fs_node_t *dir, const char *name);
int ext2_rmdir(fs_node_t *dir, const char *name);
static int ext2_dir_is_empty(fs_node_t *node);
static int ext2_add_entry(fs_node_t *dir, const char *name, uint32_t inode, uint8_t file_type);
static int ext2_remove_entry(fs_node_t *dir, const char *name);
static int ext2_flush_super(ext2_fs_t *fs);
int ext2_statfs(fs_node_t *node, struct statfs *buf);
int ext2_link(fs_node_t *parent, fs_node_t *source, const char *name);
int ext2_rename(fs_node_t *old_parent, const char *old_name, fs_node_t *new_parent, const char *new_name);
static int ext2_flush_group_desc(ext2_fs_t *fs, uint32_t group);
static uint8_t ext2_dirent_type_from_mode(uint16_t mode);
static uint8_t ext2_file_type_to_dt(uint8_t ext2_type);
static int ext2_free_indirect_tree(ext2_fs_t *fs, uint32_t block_num, uint32_t depth);
static int ext2_free_inode_blocks(ext2_fs_t *fs, ext2_inode_t *inode);
static int ext2_chmod(fs_node_t *node, uint32_t mode);
static int ext2_setattr(fs_node_t *node, const struct fs_attr *a);
static int ext2_getattr(fs_node_t *node, struct fs_attr *a);

// Helper to find a zero bit in a bitmap range
int ext2_find_next_zero_bit(void *bitmap, uint32_t total_bits, uint32_t start, uint32_t end, uint32_t *found_idx) {
    uint32_t *bitmap32 = (uint32_t *)bitmap;
    uint32_t i = start;

    if (end > total_bits) end = total_bits;

    // Align to 32 bits
    while (i < end && (i & 31)) {
        uint32_t byte_idx = i / 8;
        uint32_t bit_idx = i % 8;

        if (!(((uint8_t*)bitmap)[byte_idx] & (1 << bit_idx))) {
            *found_idx = i;
            return 1;
        }
        i++;
    }

    // Fast path: skip full words
    for (; i + 32 <= end; i += 32) {
        if (bitmap32[i / 32] != 0xFFFFFFFF) {
            break;
        }
    }

    // Slow path for remaining bits
    for (; i < end; i++) {
        uint32_t byte_idx = i / 8;
        uint32_t bit_idx = i % 8;

        if (!(((uint8_t*)bitmap)[byte_idx] & (1 << bit_idx))) {
            *found_idx = i;
            return 1;
        }
    }

    return 0;
}

// Read a block from the device, going through the unified buffer cache.
// On cache hit the data is served from RAM with no device I/O.  On miss
// we fill the cache buffer once, then serve subsequent reads of the same
// block from memory.  The cache is keyed by (device fs_node *, block_num,
// block_size), so two callers reading the same block share one entry.
// Caching is the block layer's job (sys/drivers/storage/blkdev.c); the
// filesystem just issues byte-range device I/O.  fs->device->read transparently
// serves cached sectors and coalesces misses, so this is a plain device read.
uint32_t ext2_read_block(ext2_fs_t *fs, uint32_t block_num, void *buffer) {
    if (!fs || !fs->device || !fs->device->read) return 0;
    /* block_num frequently originates from untrusted on-disk metadata
     * (inode i_block[] entries, indirect/double/triple-indirect block
     * pointers, group-descriptor table fields).  A crafted image can
     * point these past the end of the device, turning a file read into
     * an arbitrary device-offset read.  Reject anything outside the
     * filesystem's data range.  Block 0 is never a valid target for
     * this primitive (the superblock is read directly via the device);
     * a 0 pointer in an inode means "hole" and is handled by callers
     * before they reach here, so treat 0 as out-of-range too.  Zero the
     * buffer so a rejected indirect-block read yields a hole rather than
     * stale scratch contents. */
    if (block_num == 0 || block_num >= fs->sb.s_blocks_count) {
        if (buffer) memset(buffer, 0, fs->block_size);
        return 0;
    }
    off_t offset = (off_t)block_num * fs->block_size;
    return fs->device->read(fs->device, offset, fs->block_size, buffer);
}

// Read multiple contiguous blocks in one byte-range request; the block layer
// caches and coalesces underneath.
uint32_t ext2_read_blocks(ext2_fs_t *fs, uint32_t block_num, uint32_t count, void *buffer) {
    if (!fs || !fs->device || !fs->device->read) return 0;
    /* Same untrusted-block-number guard as ext2_read_block, extended to
     * the [block_num, block_num+count) span.  Guard against overflow in
     * the end-of-range computation as well as the start. */
    if (count == 0 || block_num == 0 ||
        block_num >= fs->sb.s_blocks_count ||
        count > fs->sb.s_blocks_count - block_num) {
        if (buffer) memset(buffer, 0, fs->block_size * count);
        return 0;
    }
    off_t offset = (off_t)block_num * fs->block_size;
    return fs->device->read(fs->device, offset, fs->block_size * count, buffer);
}

// Read an inode
int ext2_read_inode(ext2_fs_t *fs, uint32_t inode_num, ext2_inode_t *inode) {
    if (!fs || inode_num == 0) return -1;
    
    // Calculate which block group the inode is in
    uint32_t group = (inode_num - 1) / fs->inodes_per_group;
    uint32_t index = (inode_num - 1) % fs->inodes_per_group;
    
    if (group >= fs->group_count) return -1;
    
    // Get inode table location from block group descriptor
    uint32_t inode_table_block = fs->bgd[group].bg_inode_table;
    
    // Calculate which block and offset within the inode table
    uint32_t inodes_per_block = fs->block_size / fs->inode_size;
    uint32_t block_offset = index / inodes_per_block;
    uint32_t inode_offset = (index % inodes_per_block) * fs->inode_size;
    
    // Read the block containing the inode
    uint8_t *block_buf = kmalloc(fs->block_size);
    if (!block_buf) return -1;

    if (ext2_read_block(fs, inode_table_block + block_offset, block_buf) != fs->block_size) {
        kfree(block_buf, fs->block_size);
        return -1;
    }
    
    uint8_t *raw = block_buf + inode_offset;

    /* Per-inode metadata_csum verification.  Algorithm matches
     * FreeBSD's ext2_ei_csum (ext2_csum.c:570):
     *   seed = crc32c(fs->csum_seed, le32(inode_num))
     *   seed = crc32c(seed,           le32(inode_generation))
     *   c    = crc32c(seed, raw[0..123])               // up to chksum_lo
     *   c    = crc32c(c,    0_u16, 2)                   // zero chksum_lo
     *   c    = crc32c(c,    raw[126..127])              // rest of legacy
     *   // for 256-byte inodes:
     *   c    = crc32c(c,    raw[128..129])              // extra_isize
     *   if extra_isize >= 4:
     *     c    = crc32c(c,  0_u16, 2)                   // zero chksum_hi
     *     c    = crc32c(c,  raw[132..inode_size-1])
     *   else:
     *     c    = crc32c(c,  raw[130..inode_size-1])
     * provided = chksum_lo (| chksum_hi << 16 if extra_isize>=4)
     * If mismatch, refuse the read so callers can't act on a
     * tampered inode.  */
    if (fs->sb.s_feature_ro_compat & EXT2F_ROCOMPAT_METADATA_CKSUM) {

        const uint16_t zero16 = 0;
        uint32_t le_inum = inode_num;
        uint32_t le_gen  = *(uint32_t *)(raw + 100);   /* i_generation @100 */
        uint32_t c = crc32c_update(fs->csum_seed, &le_inum, 4);
        c = crc32c_update(c, &le_gen, 4);
        c = crc32c_update(c, raw, 124);                 /* up to chksum_lo */
        c = crc32c_update(c, &zero16, 2);
        c = crc32c_update(c, raw + 126, 2);             /* legacy tail */
        uint16_t lo = *(uint16_t *)(raw + 124);
        uint16_t hi = 0;
        uint16_t extra_isize = 0;
        if (fs->inode_size > EXT2_GOOD_OLD_INODE_SIZE) {
            extra_isize = *(uint16_t *)(raw + 128);
            c = crc32c_update(c, raw + 128, 2);         /* extra_isize  */
            uint32_t off = 130;
            if (extra_isize >= 4) {
                hi = *(uint16_t *)(raw + 130);
                c  = crc32c_update(c, &zero16, 2);
                off = 132;
            }
            if (off < fs->inode_size)
                c = crc32c_update(c, raw + off, fs->inode_size - off);
        }
        uint32_t provided = lo;
        uint32_t calc     = c;
        if (extra_isize >= 4) provided |= ((uint32_t)hi) << 16;
        else                  calc     &= 0xFFFFu;
        if (provided != calc) {
            kprintf("ext2: inode %u csum mismatch want=%08x got=%08x\n",
                    inode_num, calc, provided);
            kfree(block_buf, fs->block_size);
            return -1;
        }
    }

    /* Read the legacy 128 bytes unconditionally; if the on-disk
     * inode is larger and the file system actually uses the extra
     * region (i_extra_isize > 0), read that too.  We cap at the
     * struct's declared size so 256-byte mkfs.ext4 inodes only
     * give us the timestamp-extra block we care about, not the
     * trailing xattr area.  */
    memset(inode, 0, sizeof(ext2_inode_t));
    uint32_t legacy = EXT2_GOOD_OLD_INODE_SIZE;
    memcpy(inode, block_buf + inode_offset, legacy);
    if (fs->inode_size > legacy) {
        uint32_t want = sizeof(ext2_inode_t) - legacy;
        uint32_t avail = fs->inode_size - legacy;
        uint32_t copy = (want < avail) ? want : avail;
        memcpy((uint8_t *)inode + legacy,
               block_buf + inode_offset + legacy, copy);
        /* If i_extra_isize claims fewer bytes than we read, zero
         * the slop so callers don't act on uninitialised on-disk
         * fields that a future mkfs may repurpose.  */
        if (inode->i_extra_isize < copy - 2 /* skip i_extra_isize self */) {
            uint32_t valid = (uint32_t)inode->i_extra_isize + 2;
            if (valid < copy) {
                memset((uint8_t *)inode + legacy + valid, 0, copy - valid);
            }
        }
    }
    kfree(block_buf, fs->block_size);
    return 0;
}

// Write a block to the device.  The block layer is write-through and keeps
// its cache coherent with the device, so the filesystem just issues the write.
uint32_t ext2_write_block(ext2_fs_t *fs, uint32_t block_num, const void *buffer) {
    if (!fs || !fs->device || !fs->device->write) return 0;
    /* Symmetric with ext2_read_block: block_num often comes from untrusted
     * on-disk metadata (indirect pointers, freshly-allocated blocks whose
     * bitmap may be corrupt).  Reject block 0 and anything past the data range
     * so a bad pointer cannot scribble an arbitrary device offset. */
    if (block_num == 0 || block_num >= fs->sb.s_blocks_count) {
        return 0;
    }
    off_t offset = (off_t)block_num * fs->block_size;
    return fs->device->write(fs->device, offset, fs->block_size, (uint8_t *)buffer);
}

static int is_sparse_backup(uint32_t group) {
    if (group <= 1) return 1;
    for (uint32_t p = 3; p <= 7; p += 2) {
        uint32_t val = p;
        while (val < group) val *= p;
        if (val == group) return 1;
    }
    return 0;
}

/*
 * The on-disk superblock is 1024 bytes, but ext2_superblock_t only describes
 * the leading fields this driver actually models -- it ends at s_algo_bitmap.
 * Writing 1024 bytes straight out of &fs->sb therefore read far past the
 * struct (and past the ext2_fs_t allocation holding it), publishing kernel
 * heap -- including the fs->bgd / fs->device pointers -- into the filesystem
 * image, and overwriting every extended field we do not model: s_hash_seed,
 * s_desc_size, s_checksum_seed, s_default_mount_opts and s_checksum.  On a
 * metadata_csum volume that alone makes the next mount fail validation.
 *
 * So the superblock is always written read-modify-write: pull the existing
 * 1024 bytes, patch in the sizeof(fs->sb) prefix we own, and put it back.
 */
static int ext2_super_rmw(ext2_fs_t *fs, uint8_t *buf, size_t buf_len) {
    if (buf_len < sizeof(fs->sb)) return -EIO;

    if (!fs->device->read ||
        fs->device->read(fs->device, 1024, buf_len, buf) != buf_len) {
        /* Nothing readable to preserve: start from zeros rather than from
         * whatever the allocator last left in this buffer. */
        memset(buf, 0, buf_len);
    }
    memcpy(buf, &fs->sb, sizeof(fs->sb));
    return 0;
}

static int ext2_flush_super(ext2_fs_t *fs) {
    if (!fs || !fs->device || !fs->device->write) return -EIO;

    // Write primary superblock
    uint8_t *sb_buf = kmalloc(1024);
    if (!sb_buf) return -ENOMEM;

    if (ext2_super_rmw(fs, sb_buf, 1024) != 0) {
        kfree(sb_buf, 1024);
        return -EIO;
    }

    if (fs->device->write(fs->device, 1024, 1024, sb_buf) != 1024) {
        kfree(sb_buf, 1024);
        return -EIO;
    }
    kfree(sb_buf, 1024);

    // Write backups if necessary (EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER = 0x0001)
    int sparse = (fs->sb.s_feature_ro_compat & 0x0001);
    
    int backup_err = 0;
    for (uint32_t i = 1; i < fs->group_count; i++) {
        if (sparse && !is_sparse_backup(i)) continue;

        uint32_t block = i * fs->sb.s_blocks_per_group + fs->sb.s_first_data_block;
        // Superblock backups are at the start of the block
        uint8_t *tmp = kmalloc(fs->block_size);
        if (!tmp) {
            backup_err = -ENOMEM;
            continue;
        }
        /* Same read-modify-write as the primary: copying 1024 bytes out of
         * a 204-byte struct over-reads, and blindly zeroing the rest of the
         * block would wipe the backup's extended superblock fields too. */
        if (ext2_read_block(fs, block, tmp) != fs->block_size) {
            memset(tmp, 0, fs->block_size);
        }
        memcpy(tmp, &fs->sb, sizeof(fs->sb));
        /* Don't drop write errors silently — a failed backup write
         * leaves the on-disk image inconsistent with the primary. */
        if (ext2_write_block(fs, block, tmp) != fs->block_size) {
            backup_err = -EIO;
        }
        kfree(tmp, fs->block_size);
    }

    return backup_err;
}

static int ext2_flush_group_desc(ext2_fs_t *fs, uint32_t group) {
    uint32_t bgd_block;
    uint32_t desc_offset;
    uint32_t block_offset;
    uint8_t *block_buf;
    int ret = 0;

    if (!fs || group >= fs->group_count) return -EINVAL;

    bgd_block = (fs->block_size == 1024) ? 2 : 1;
    /* On-disk descriptors are fs->desc_size apart (32 normally, 64 on an
     * INCOMPAT_64BIT filesystem) — the GDT read at mount already strides by
     * desc_size, so the write must too, or we'd scatter each group's 32 live
     * bytes at the wrong offset and corrupt the descriptor table.  Only the
     * low 32 bytes (the fields the kernel tracks) are copied; the read-modify-
     * write below preserves the high half (64-bit pointers / csum). */
    desc_offset = group * fs->desc_size;
    block_offset = desc_offset % fs->block_size;
    block_buf = kmalloc(fs->block_size);
    if (!block_buf) return -ENOMEM;

    if (ext2_read_block(fs, bgd_block + (desc_offset / fs->block_size), block_buf) != fs->block_size) {
        ret = -EIO;
        goto out;
    }

    memcpy(block_buf + block_offset, &fs->bgd[group], sizeof(ext2_group_desc_t));
    if (ext2_write_block(fs, bgd_block + (desc_offset / fs->block_size), block_buf) != fs->block_size) {
        ret = -EIO;
        goto out;
    }

out:
    kfree(block_buf, fs->block_size);
    return ret;
}

/*
 * Flush deferred metadata: every group descriptor marked dirty plus the
 * superblock (primary + sparse backups) if its free counts changed.  Called
 * on the dirty-op threshold, on sync(2)/fsync, and on unmount.  The block and
 * inode bitmaps are already write-through per allocation, so this only
 * reconciles the cached free counts — the expensive part that used to run on
 * every alloc/free.
 */
int ext2_sync_meta(ext2_fs_t *fs) {
    if (!fs) return -EINVAL;
    if (fs->readonly) return 0;
    int err = 0;
    if (fs->bgd_dirty) {
        for (uint32_t g = 0; g < fs->group_count; g++) {
            if (fs->bgd_dirty[g >> 3] & (uint8_t)(1u << (g & 7))) {
                if (ext2_flush_group_desc(fs, g) != 0) err = -EIO;
                fs->bgd_dirty[g >> 3] &= (uint8_t)~(1u << (g & 7));
            }
        }
    }
    if (fs->sb_dirty) {
        if (ext2_flush_super(fs) != 0) err = -EIO;
        fs->sb_dirty = 0;
    }
    fs->meta_dirty_ops = 0;
    return err;
}

/*
 * Mark group `group`'s descriptor and the superblock free counts dirty,
 * deferring the on-disk flush.  Replaces a per-alloc ext2_flush_group_desc()
 * + ext2_flush_super() (the latter rewriting the primary super AND every
 * sparse-super backup with a kmalloc each) — the flush storm that made file
 * creation (tar, builds, installs) pathologically slow.  Coalesce, then
 * flush once enough have accumulated to bound staleness.
 */
#define EXT2_META_FLUSH_THRESHOLD 256
static void ext2_mark_meta_dirty(ext2_fs_t *fs, uint32_t group) {
    if (!fs) return;
    if (!fs->bgd_dirty) {
        /* No deferral bitmap (allocation failed at mount) — flush immediately
         * so group descriptors are never left unreconciled. */
        ext2_flush_group_desc(fs, group);
        ext2_flush_super(fs);
        return;
    }
    if (group < fs->group_count)
        fs->bgd_dirty[group >> 3] |= (uint8_t)(1u << (group & 7));
    fs->sb_dirty = 1;
    if (++fs->meta_dirty_ops >= EXT2_META_FLUSH_THRESHOLD)
        ext2_sync_meta(fs);
}

// Write an inode back to disk
int ext2_write_inode(ext2_fs_t *fs, uint32_t inode_num, ext2_inode_t *inode) {
    if (!fs || inode_num == 0) return -1;
    
    // Calculate which block group the inode is in
    uint32_t group = (inode_num - 1) / fs->inodes_per_group;
    uint32_t index = (inode_num - 1) % fs->inodes_per_group;
    
    if (group >= fs->group_count) return -1;
    
    // Get inode table location from block group descriptor
    uint32_t inode_table_block = fs->bgd[group].bg_inode_table;
    
    // Calculate which block and offset within the inode table
    uint32_t inodes_per_block = fs->block_size / fs->inode_size;
    uint32_t block_offset = index / inodes_per_block;
    uint32_t inode_offset = (index % inodes_per_block) * fs->inode_size;
    
    // Read the block containing the inode
    uint8_t *block_buf = kmalloc(fs->block_size);
    if (!block_buf) return -1;

    /* FS-02: the read..modify..write below touches a whole inode-table block
     * shared by many inodes.  Serialize it so a concurrent writer of a
     * co-resident inode can't clobber the copy we're committing (and vice
     * versa).  Held across both the read and the write; released on every
     * exit past this point. */
    mutex_lock(&ext2_inode_table_lock);

    if (ext2_read_block(fs, inode_table_block + block_offset, block_buf) != fs->block_size) {
        mutex_unlock(&ext2_inode_table_lock);
        kfree(block_buf, fs->block_size);
        return -1;
    }

    /* ext2trace: catch the moment a live inode changes file type
     * between two VALID types (REG <-> DIR, REG <-> LNK, etc).
     * Live-to-live transitions without an intervening free are the
     * corruption signal.
     *
     * Transient zeroing (live -> mode==0) is NOT corruption: it's
     * how ext2_alloc_inode initializes a freshly-allocated slot
     * whose on-disk inode still carries the previous file's
     * mode bits (ext2_free_inode only clears the bitmap, doesn't
     * scrub the inode struct).  Similarly, alloc-init -> real mode
     * (mode==0 -> S_IFREG/S_IFDIR/...) is the normal post-alloc
     * mknod write that establishes the type. */
    if (ext2_trace_on()) {
        uint16_t old_mode = *(uint16_t *)(block_buf + inode_offset);
        uint16_t new_mode = inode->i_mode;
        if (old_mode != 0 && new_mode != 0 &&
            (old_mode & S_IFMT) != (new_mode & S_IFMT)) {
            void *caller = __builtin_return_address(0);
            kprintf("ext2trace: TYPE FLIP inode=%u %s(mode=%#o) -> %s(mode=%#o) caller=%p\n",
                    inode_num,
                    ext2_trace_iftype(old_mode), old_mode,
                    ext2_trace_iftype(new_mode), new_mode,
                    caller);
        }
    }

    /* Read-modify-write: preserve everything past sizeof(ext2_inode_t)
     * (xattr area on 256-byte inodes) and everything past inode_size
     * when we'd otherwise overshoot.  For 128-byte mounts we only
     * touch the legacy area.  For 256-byte mounts we touch through
     * i_crtime_extra (offset 148) and leave the xattr block alone.  */
    uint32_t write_bytes = sizeof(ext2_inode_t);
    if (write_bytes > fs->inode_size) write_bytes = fs->inode_size;
    /* Make sure i_extra_isize correctly advertises how many post-128
     * bytes we are about to commit.  Skip the i_extra_isize field
     * itself (2 bytes) when counting — the on-disk meaning is "bytes
     * past offset 130 that are in use".  */
    if (fs->inode_size > EXT2_GOOD_OLD_INODE_SIZE &&
        write_bytes > EXT2_GOOD_OLD_INODE_SIZE) {
        uint16_t want = write_bytes - EXT2_GOOD_OLD_INODE_SIZE - 2;
        if (inode->i_extra_isize < want) inode->i_extra_isize = want;
    }
    memcpy(block_buf + inode_offset, inode, write_bytes);

    /* metadata_csum: recompute the per-inode csum over what we're
     * about to commit, and patch chksum_lo (and chksum_hi if the
     * inode is 256-byte AND extra_isize claims those bytes are
     * valid).  Without this the next read_inode would fail csum
     * verify after any setattr/chmod/utime write.  */
    if (fs->sb.s_feature_ro_compat & EXT2F_ROCOMPAT_METADATA_CKSUM) {

        uint8_t *raw = block_buf + inode_offset;
        const uint16_t zero16 = 0;
        /* Zero the csum bytes in the buffer before the calculation
         * so the algorithm replays the same "csum-field-cleared"
         * shape the verifier expects.  */
        *(uint16_t *)(raw + 124) = 0;
        uint16_t prev_hi = 0;
        int has_hi = 0;
        if (fs->inode_size > EXT2_GOOD_OLD_INODE_SIZE) {
            uint16_t extra = *(uint16_t *)(raw + 128);
            if (extra >= 4) { has_hi = 1; prev_hi = *(uint16_t *)(raw + 130); (void)prev_hi; *(uint16_t *)(raw + 130) = 0; }
        }
        uint32_t le_inum = inode_num;
        uint32_t le_gen  = *(uint32_t *)(raw + 100);
        uint32_t c = crc32c_update(fs->csum_seed, &le_inum, 4);
        c = crc32c_update(c, &le_gen, 4);
        c = crc32c_update(c, raw, 124);
        c = crc32c_update(c, &zero16, 2);
        c = crc32c_update(c, raw + 126, 2);
        if (fs->inode_size > EXT2_GOOD_OLD_INODE_SIZE) {
            c = crc32c_update(c, raw + 128, 2);
            uint32_t off = 130;
            if (has_hi) {
                c = crc32c_update(c, &zero16, 2);
                off = 132;
            }
            if (off < fs->inode_size)
                c = crc32c_update(c, raw + off, fs->inode_size - off);
        }
        *(uint16_t *)(raw + 124) = (uint16_t)(c & 0xFFFFu);
        if (has_hi)
            *(uint16_t *)(raw + 130) = (uint16_t)((c >> 16) & 0xFFFFu);
    }
    
    // Write the block back to disk
    if (ext2_write_block(fs, inode_table_block + block_offset, block_buf) != fs->block_size) {
        mutex_unlock(&ext2_inode_table_lock);
        kfree(block_buf, fs->block_size);
        return -1;
    }

    mutex_unlock(&ext2_inode_table_lock);
    kfree(block_buf, fs->block_size);
    return 0;
}

// Get block number for a given file block index (handles indirect blocks)
// Requires caller-provided scratch buffers (each size of block_size)
/* ext4 extent-tree resolver — translate a logical file-block index
 * into the physical block on disk by walking the inline extent
 * header at i_block[] and (if depth > 0) chasing index nodes down
 * to the leaf level.  Returns 0 for holes (logical block not
 * covered by any extent) or for any error.
 *
 * Substrate's block addresses are uint32_t, so the high 16 bits of
 * the ext4 48-bit physical address are clamped — fine for any
 * filesystem under 16 TiB.  Add 64-bit-block lookup when we
 * actually grow a >2^32-block target.  */
static uint32_t ext4_extent_resolve(ext2_fs_t *fs, ext2_inode_t *inode,
                                    uint32_t logical, uint8_t *scratch) {
    /* Start at the inline header in the inode itself.  */
    ext4_extent_header_t *eh = (ext4_extent_header_t *)&inode->i_block[0];
    if (eh->eh_magic != EXT4_EXT_MAGIC) return 0;

    /* Walk indexes down to depth 0.  At each level pick the rightmost
     * index whose ei_blk <= logical — that's the child covering this
     * logical block.  */
    uint8_t *node = (uint8_t *)&inode->i_block[0];
    /* Bound the on-disk entry count to what the containing buffer can hold:
     * the root header lives in the 60-byte i_block area, deeper nodes in a
     * block-sized scratch buffer.  eh_ecount is an unbounded u16 from disk, so
     * a corrupt filesystem could otherwise drive the idx[i]/ex[i] loops far
     * past the buffer — an out-of-bounds read. */
    size_t node_cap = sizeof(inode->i_block);
    /* EXT2-13: eh_depth is an unbounded u16 from disk and drives this descent.
     * A corrupt inode claiming depth 65535, with an index entry pointing back
     * at its own block, made every logical-block lookup do 65535 block reads.
     * ext4 itself caps the tree at EXT4_EXT_DEPTH_MAX levels, so anything
     * deeper is corruption, not a filesystem we should try to walk. */
    if (eh->eh_depth > EXT4_EXT_DEPTH_MAX) return 0;
    for (int depth = eh->eh_depth; depth > 0; depth--) {
        ext4_extent_idx_t *idx = (ext4_extent_idx_t *)(node + sizeof(*eh));
        int n = eh->eh_ecount;
        if (n <= 0) return 0;
        size_t max_idx = (node_cap > sizeof(*eh))
                       ? (node_cap - sizeof(*eh)) / sizeof(ext4_extent_idx_t) : 0;
        if ((size_t)n > max_idx) n = (int)max_idx;
        if (n <= 0) return 0;
        int pick = -1;
        for (int i = 0; i < n; i++) {
            if (idx[i].ei_blk <= logical) pick = i;
            else break;
        }
        if (pick < 0) return 0;
        uint64_t child = ((uint64_t)idx[pick].ei_leaf_hi << 32)
                       | idx[pick].ei_leaf_lo;
        if (child == 0 || child >> 32) return 0;
        ext2_read_block(fs, (uint32_t)child, scratch);
        node = scratch;
        node_cap = fs->block_size;
        eh = (ext4_extent_header_t *)node;
        if (eh->eh_magic != EXT4_EXT_MAGIC) return 0;
        /* EXT2-13: a well-formed tree strictly decreases in depth on the way
         * down.  Without this an index pointing at its own block (or at any
         * node of equal-or-greater depth) satisfies the magic check and the
         * loop just keeps re-reading it. */
        if (eh->eh_depth != depth - 1) return 0;
    }

    /* Leaf level: array of extents.  Each extent covers
     * [e_blk, e_blk + e_len).  Find the one containing `logical`.  */
    ext4_extent_t *ex = (ext4_extent_t *)(node + sizeof(*eh));
    int n = eh->eh_ecount;
    {
        size_t max_ex = (node_cap > sizeof(*eh))
                      ? (node_cap - sizeof(*eh)) / sizeof(ext4_extent_t) : 0;
        if ((size_t)n > max_ex) n = (int)max_ex;
    }
    for (int i = 0; i < n; i++) {
        uint32_t ext_start = ex[i].e_blk;
        /* Uninitialised extent's e_len has the high bit set; mask
         * it for length math (data still reads as zeros, but the
         * extent does cover the range).  */
        uint32_t len = ex[i].e_len & 0x7FFF;
        if (logical >= ext_start && logical < ext_start + len) {
            uint64_t phys = ((uint64_t)ex[i].e_start_hi << 32)
                          | ex[i].e_start_lo;
            phys += (logical - ext_start);
            if (phys >> 32) return 0;   /* needs 64-bit block addr */
            return (uint32_t)phys;
        }
    }
    /* Hole.  Sparse files have unreferenced ranges; the caller
     * reads zeros for them.  */
    return 0;
}

uint32_t ext2_get_block_num(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t block_idx,
                                   uint32_t *indirect_buf, uint32_t *dindirect_buf, uint32_t *tindirect_buf) {
    /* ext4 extent-tree path takes over completely when the flag is
     * set on the inode.  i_block[] is overlaid with the extent
     * header in that case, NOT the legacy 12+1+1+1 array.  */
    if (inode->i_flags & EXT4_EXTENTS_FL) {
        return ext4_extent_resolve(fs, inode, block_idx,
                                   (uint8_t *)indirect_buf);
    }

    uint32_t ptrs_per_block = fs->block_size / 4;

    // Direct blocks (0-11)
    if (block_idx < 12) {
        return inode->i_block[block_idx];
    }
    block_idx -= 12;
    
    // Indirect block (12)
    if (block_idx < ptrs_per_block) {
        if (inode->i_block[12] == 0) return 0;
        ext2_read_block(fs, inode->i_block[12], (uint8_t *)indirect_buf);
        return indirect_buf[block_idx];
    }
    block_idx -= ptrs_per_block;
    
    // Double indirect block (13)
    if (block_idx < ptrs_per_block * ptrs_per_block) {
        if (inode->i_block[13] == 0) return 0;
        ext2_read_block(fs, inode->i_block[13], (uint8_t *)dindirect_buf);
        uint32_t indirect_idx = block_idx / ptrs_per_block;
        uint32_t direct_idx = block_idx % ptrs_per_block;
        uint32_t indirect_block = dindirect_buf[indirect_idx];

        if (indirect_block == 0) return 0;

        ext2_read_block(fs, indirect_block, (uint8_t *)indirect_buf);
        return indirect_buf[direct_idx];
    }
    block_idx -= ptrs_per_block * ptrs_per_block;
    
    // Triple indirect block (14)
    if (block_idx < ptrs_per_block * ptrs_per_block * ptrs_per_block) {
        if (inode->i_block[14] == 0) return 0;
        ext2_read_block(fs, inode->i_block[14], (uint8_t *)tindirect_buf);
        
        uint32_t tindirect_idx = block_idx / (ptrs_per_block * ptrs_per_block);
        uint32_t remaining = block_idx % (ptrs_per_block * ptrs_per_block);
        uint32_t dindirect_idx = remaining / ptrs_per_block;
        uint32_t indirect_idx = remaining % ptrs_per_block;
        
        uint32_t dindirect_block = tindirect_buf[tindirect_idx];
        if (dindirect_block == 0) return 0;

        ext2_read_block(fs, dindirect_block, (uint8_t *)dindirect_buf);
        uint32_t indirect_block = dindirect_buf[dindirect_idx];
        if (indirect_block == 0) return 0;

        ext2_read_block(fs, indirect_block, (uint8_t *)indirect_buf);
        return indirect_buf[indirect_idx];
    }
    
    return 0;
}

// Get contiguous extent of blocks
void ext2_get_blocks_extent(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t block_idx, uint32_t max_count, 
                                   uint32_t *phys_block, uint32_t *count,
                                   uint32_t *indirect_buf, uint32_t *dindirect_buf, uint32_t *tindirect_buf) {
    *count = 0;
    *phys_block = 0;
    if (max_count == 0) return;

    *phys_block = ext2_get_block_num(fs, inode, block_idx, indirect_buf, dindirect_buf, tindirect_buf);
    *count = 1;
    
    if (*phys_block == 0) {
        // Find sparse run
        for (uint32_t i = 1; i < max_count; i++) {
            if (ext2_get_block_num(fs, inode, block_idx + i, indirect_buf, dindirect_buf, tindirect_buf) != 0) break;
            (*count)++;
        }
        return;
    }

    // Find contiguous run
    for (uint32_t i = 1; i < max_count; i++) {
        uint32_t next = ext2_get_block_num(fs, inode, block_idx + i, indirect_buf, dindirect_buf, tindirect_buf);
        if (next == *phys_block + i) {
            (*count)++;
        } else {
            break;
        }
    }
}

// Read data from an inode at a given offset
uint32_t ext2_inode_read(ext2_node_t *node, off_t offset, uint32_t size, void *buffer) {
    /* off_t is signed and sys_lseek() accepts a negative offset, which
     * read_fs()/write_fs() pass straight through.  Every bound below is an
     * unsigned comparison that a negative value silently passes, after which
     * block_offset = (uint32_t)(offset % block_size) is huge and
     * memcpy(block_buf + block_offset, ...) lands BEFORE the kmalloc'd block
     * buffer -- an attacker-controlled write (or heap disclosure on read)
     * reachable from any unprivileged process via
     * lseek(fd, -100, SEEK_SET); write(fd, buf, 64). */
    if (offset < 0) return 0;
    ext2_fs_t *fs = node->fs;
    ext2_inode_t *inode = &node->inode;

    if (offset >= inode->i_size) return 0;
    if (offset + size > inode->i_size) size = inode->i_size - offset;
    
    mutex_lock(&node->lock);

    // Lazy allocate scratch buffers
    uint32_t block_size = fs->block_size;
    if (!node->block_buf) node->block_buf = kmalloc(block_size);
    if (!node->indirect_buf) node->indirect_buf = kmalloc(block_size);
    if (!node->dindirect_buf) node->dindirect_buf = kmalloc(block_size);
    if (!node->tindirect_buf) node->tindirect_buf = kmalloc(block_size);

    if (!node->block_buf || !node->indirect_buf || !node->dindirect_buf || !node->tindirect_buf) {
        mutex_unlock(&node->lock);
        return 0;
    }

    uint8_t *block_buf = node->block_buf;
    uint32_t *indirect = node->indirect_buf;
    uint32_t *dindirect = node->dindirect_buf;
    uint32_t *tindirect = node->tindirect_buf;

    uint8_t *buf = (uint8_t *)buffer;
    uint32_t total_read = 0;
    
    while (size > 0) {
        uint32_t block_idx = (uint32_t)(offset / fs->block_size);
        uint32_t block_offset = (uint32_t)(offset % fs->block_size);
        
        uint32_t phys_block;
        uint32_t contiguous_blocks;
        uint32_t needed_blocks = (size + block_offset + fs->block_size - 1) / fs->block_size;
        if (needed_blocks > 1024) needed_blocks = 1024;
        
        ext2_get_blocks_extent(fs, inode, block_idx, needed_blocks, &phys_block, &contiguous_blocks, indirect, dindirect, tindirect);
        
        if (contiguous_blocks == 0) break; // Should not happen

        // 1. Handle Unaligned Head
        if (block_offset > 0) {
            if (phys_block == 0) memset(block_buf, 0, fs->block_size);
            else ext2_read_block(fs, phys_block, block_buf);

            uint32_t copy = fs->block_size - block_offset;
            if (copy > size) copy = size;
            memcpy(buf, block_buf + block_offset, copy);

            buf += copy; offset += copy; size -= copy;
            total_read += copy;

            if (phys_block != 0) phys_block++;
            contiguous_blocks--;
            block_offset = 0;
            if (size == 0 || contiguous_blocks == 0) continue;
        }

        // 2. Handle Aligned Body
        if (contiguous_blocks > 0 && size >= fs->block_size) {
            uint32_t full_blocks = size / fs->block_size;
            if (full_blocks > contiguous_blocks) full_blocks = contiguous_blocks;

            if (phys_block == 0) {
                memset(buf, 0, full_blocks * fs->block_size);
            } else {
                ext2_read_blocks(fs, phys_block, full_blocks, buf);
            }

            uint32_t bytes = full_blocks * fs->block_size;
            buf += bytes; offset += bytes; size -= bytes;
            total_read += bytes;

            if (phys_block != 0) phys_block += full_blocks;
            contiguous_blocks -= full_blocks;
        }

        // 3. Handle Tail
        if (contiguous_blocks > 0 && size > 0) {
            if (phys_block == 0) memset(block_buf, 0, fs->block_size);
            else ext2_read_block(fs, phys_block, block_buf);

            memcpy(buf, block_buf, size);
            buf += size; offset += size; 
            total_read += size;
            size = 0;
        }
    }
    
    mutex_unlock(&node->lock);
    return total_read;
}

/* Extent-tree block allocator — append path only.
 *
 * Handles three cases on an inline depth-0 extent header:
 *   1. Extent list is empty: create a single-block extent covering
 *      block_idx.  Always succeeds if disk has any free block.
 *   2. The last extent's logical end == block_idx AND a contiguous
 *      physical block (e_start + e_len) is still free: extend the
 *      extent's e_len by 1 in place.  Cheapest case.
 *   3. The last extent's logical end == block_idx but contiguous
 *      isn't free / e_len would overflow uint16_t: create a fresh
 *      single-block extent if eh_max > eh_ecount.  Refuse if the
 *      header is full (would need to grow the tree into a leaf
 *      block — not implemented; falls back to -EROFS).
 *
 * Anything else (sparse writes, multi-level trees, writes into the
 * middle of an existing extent) refuses with -EROFS so file_write
 * fails cleanly instead of silently corrupting the extent tree.
 * Returns 0 on success, -1 on out-of-space / refusal.  */
static int ext4_extent_alloc_inode_block(ext2_fs_t *fs, ext2_inode_t *inode,
                                         uint32_t block_idx) {
    uint8_t *h = (uint8_t *)inode->i_block;
    ext4_extent_header_t *eh = (ext4_extent_header_t *)h;
    if (eh->eh_magic != EXT4_EXT_MAGIC)             return -1;
    if (eh->eh_depth != 0) {
        /*
         * Implementing ext4 multi-level extent tree updates is complex and bug-prone,
         * requiring traversing and splitting extent index blocks. It is deliberately
         * unsupported.
         */

        kprintf("ext2: multi-level extent tree updates are unsupported\n");
        return -1;
    }
    uint32_t sectors_per_block = fs->block_size / 512;

    ext4_extent_t *exts = (ext4_extent_t *)(h + sizeof(*eh));
    uint16_t n = eh->eh_ecount;
    uint16_t max = eh->eh_max;

    /*
     * eh_ecount and eh_max come straight off the disk and are used below as
     * indices into the inline extent array, which lives inside the 60-byte
     * i_block[].  ext4_extent_resolve() clamps them; this allocator did not,
     * so an inode claiming eh_ecount = 500 made `&exts[n-1]` a read and
     * `&exts[n]` a 12-byte WRITE several kilobytes past the inode -- straight
     * through the static ext2_node_cache[] into neighbouring slots.
     */
    uint16_t capacity = (uint16_t)((sizeof(inode->i_block) - sizeof(*eh)) /
                                   sizeof(ext4_extent_t));
    if (max > capacity || n > max) {
        kprintf("ext2: bogus inline extent header (ecount=%u max=%u cap=%u)\n",
                n, max, capacity);
        return -1;
    }

    if (n == 0) {
        if (max == 0) return -1;
        uint32_t blk = ext2_alloc_block(fs);
        if (blk == 0) return -1;
        exts[0].e_blk     = block_idx;
        exts[0].e_len     = 1;
        exts[0].e_start_hi = 0;
        exts[0].e_start_lo = blk;
        eh->eh_ecount = 1;
        inode->i_blocks += sectors_per_block;
        return 0;
    }

    /* Append-only check.  The "last extent" is the one whose
     * logical range ends highest.  In a sane on-disk layout the
     * entries are sorted by e_blk so exts[n-1] is the last; we
     * don't bother verifying.  */
    ext4_extent_t *last = &exts[n - 1];
    uint32_t logical_end = last->e_blk + last->e_len;
    if (logical_end != block_idx) return -1;    /* sparse — refuse */

    /* EXT2-11: e_start_lo holds the low 32 bits and e_start_hi the *high 16*
     * of a 48-bit physical block number, which is how ext4_extent_resolve()
     * reads it back ((hi << 32) | lo).  This shifted hi by 16 instead, so on
     * any volume that actually used the high word the contiguity test
     * compared against a garbage address. */
    uint64_t phys_end = ((uint64_t)last->e_start_hi << 32) | last->e_start_lo;
    phys_end += last->e_len;

    uint32_t blk = ext2_alloc_block(fs);
    if (blk == 0) return -1;
    inode->i_blocks += sectors_per_block;

    if (blk == phys_end && last->e_len < 0x7FFF) {
        /* Case 2: contiguous extension.  Grow the existing extent's
         * length and call it a day.  No tree-structure change.  */
        last->e_len = last->e_len + 1;
        return 0;
    }

    if (n >= max) {
        /* Case 4: header is full and the allocator didn't give us a
         * contiguous block.  Releasing `blk` here would be the right
         * thing to do; substrate's ext2_free_block does that — call
         * it so the disk doesn't leak.  */
        ext2_free_block(fs, blk);
        inode->i_blocks -= sectors_per_block;
        return -1;
    }

    /* Case 3: new single-block extent.  */
    ext4_extent_t *ne = &exts[n];
    ne->e_blk      = block_idx;
    ne->e_len      = 1;
    /* EXT2-11: `blk` is a 32-bit block number, so it belongs entirely in
     * e_start_lo with a zero high word.  Writing (blk >> 16) into e_start_hi
     * meant ext4_extent_resolve() read it back as bits 32-47 of a 48-bit
     * address: for any block above 65535 the resolved address had a non-zero
     * high word, hit the `if (phys >> 32) return 0` guard and was reported as
     * a hole -- the data was unreadable and the block leaked, then got
     * re-allocated on the next write. */
    ne->e_start_hi = 0;
    ne->e_start_lo = blk;
    eh->eh_ecount  = n + 1;
    return 0;
}

// Allocate and add a block to an inode
int ext2_alloc_inode_block(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t block_idx, uint32_t *indirect, uint32_t *dindirect, uint32_t *tindirect) {
    /* Extent-tree files route through the extent allocator instead
     * of the legacy block-pointer arithmetic — i_block[] doesn't
     * hold direct/indirect addresses for these inodes.  Append-only
     * for now; sparse / multi-level / split / grow are all in the
     * "TODO: extent-tree WRITE path" task.  */
    if (inode->i_flags & EXT4_EXTENTS_FL)
        return ext4_extent_alloc_inode_block(fs, inode, block_idx);

    uint32_t ptrs_per_block = fs->block_size / 4;
    uint32_t sectors_per_block = fs->block_size / 512;

    // Direct blocks (0-11)
    if (block_idx < 12) {
        uint32_t new_block = ext2_alloc_block(fs);
        if (new_block == 0) return -1;
        inode->i_block[block_idx] = new_block;
        inode->i_blocks += sectors_per_block;
        return 0;
    }
    block_idx -= 12;
    
    // Indirect block (12)
    if (block_idx < ptrs_per_block) {
        if (inode->i_block[12] == 0) {
            uint32_t new_indirect = ext2_alloc_block(fs);
            if (new_indirect == 0) return -1;
            inode->i_block[12] = new_indirect;
            inode->i_blocks += sectors_per_block;
            memset(indirect, 0, fs->block_size);
            ext2_write_block(fs, new_indirect, indirect);
        } else {
            ext2_read_block(fs, inode->i_block[12], (uint8_t *)indirect);
        }
        
        uint32_t new_block = ext2_alloc_block(fs);
        if (new_block == 0) return -1;
        indirect[block_idx] = new_block;
        inode->i_blocks += sectors_per_block;
        ext2_write_block(fs, inode->i_block[12], (uint8_t *)indirect);
        return 0;
    }
    block_idx -= ptrs_per_block;
    
    // Double indirect block (13)
    if (block_idx < ptrs_per_block * ptrs_per_block) {
        if (inode->i_block[13] == 0) {
            uint32_t new_dindirect = ext2_alloc_block(fs);
            if (new_dindirect == 0) return -1;
            inode->i_block[13] = new_dindirect;
            inode->i_blocks += sectors_per_block;
            memset(dindirect, 0, fs->block_size);
            ext2_write_block(fs, new_dindirect, (uint8_t *)dindirect);
        } else {
            ext2_read_block(fs, inode->i_block[13], (uint8_t *)dindirect);
        }
        
        uint32_t di_idx = block_idx / ptrs_per_block;
        uint32_t off = block_idx % ptrs_per_block;
        
        if (dindirect[di_idx] == 0) {
            uint32_t new_indirect = ext2_alloc_block(fs);
            if (new_indirect == 0) return -1;
            dindirect[di_idx] = new_indirect;
            inode->i_blocks += sectors_per_block;
            ext2_write_block(fs, inode->i_block[13], (uint8_t *)dindirect);
            memset(indirect, 0, fs->block_size);
            ext2_write_block(fs, new_indirect, (uint8_t *)indirect);
        } else {
            ext2_read_block(fs, dindirect[di_idx], (uint8_t *)indirect);
        }
        
        uint32_t new_block = ext2_alloc_block(fs);
        if (new_block == 0) return -1;
        indirect[off] = new_block;
        inode->i_blocks += sectors_per_block;
        ext2_write_block(fs, dindirect[di_idx], (uint8_t *)indirect);
        return 0;
    }
    block_idx -= ptrs_per_block * ptrs_per_block;
    
    // Triple indirect block (14)
    if (block_idx < ptrs_per_block * ptrs_per_block * ptrs_per_block) {
        if (inode->i_block[14] == 0) {
            uint32_t new_tindirect = ext2_alloc_block(fs);
            if (new_tindirect == 0) return -1;
            inode->i_block[14] = new_tindirect;
            inode->i_blocks += sectors_per_block;
            memset(tindirect, 0, fs->block_size);
            ext2_write_block(fs, new_tindirect, (uint8_t *)tindirect);
        } else {
            ext2_read_block(fs, inode->i_block[14], (uint8_t *)tindirect);
        }
        
        uint32_t ddi_idx = block_idx / (ptrs_per_block * ptrs_per_block);
        uint32_t rem = block_idx % (ptrs_per_block * ptrs_per_block);
        
        if (tindirect[ddi_idx] == 0) {
            uint32_t new_dindirect = ext2_alloc_block(fs);
            if (new_dindirect == 0) return -1;
            tindirect[ddi_idx] = new_dindirect;
            inode->i_blocks += sectors_per_block;
            ext2_write_block(fs, inode->i_block[14], (uint8_t *)tindirect);
            memset(dindirect, 0, fs->block_size);
            ext2_write_block(fs, new_dindirect, (uint8_t *)dindirect);
        } else {
            ext2_read_block(fs, tindirect[ddi_idx], (uint8_t *)dindirect);
        }
        
        uint32_t di_idx = rem / ptrs_per_block;
        uint32_t off = rem % ptrs_per_block;
        
        if (dindirect[di_idx] == 0) {
            uint32_t new_indirect = ext2_alloc_block(fs);
            if (new_indirect == 0) return -1;
            dindirect[di_idx] = new_indirect;
            inode->i_blocks += sectors_per_block;
            ext2_write_block(fs, tindirect[ddi_idx], (uint8_t *)dindirect);
            memset(indirect, 0, fs->block_size);
            ext2_write_block(fs, new_indirect, (uint8_t *)indirect);
        } else {
            ext2_read_block(fs, dindirect[di_idx], (uint8_t *)indirect);
        }
        
        uint32_t new_block = ext2_alloc_block(fs);
        if (new_block == 0) return -1;
        indirect[off] = new_block;
        inode->i_blocks += sectors_per_block;
        ext2_write_block(fs, dindirect[di_idx], (uint8_t *)indirect);
        return 0;
    }
    
    return -EFBIG;
}

// Write data to an inode at a given offset
uint32_t ext2_inode_write(ext2_node_t *node, off_t offset, uint32_t size,
                          const void *buffer, int *errp) {
    /* EXT2-15: every early return below used to yield a bare 0, which
     * ext2_file_write() handed straight back to write(2).  POSIX forbids a 0
     * return for a non-zero count, and stdio's fwrite() retry loop treats it
     * as "try again" -- so a full filesystem spun forever instead of
     * reporting ENOSPC.  Report the reason through errp. */
    if (errp) *errp = 0;
    /* off_t is signed and sys_lseek() accepts a negative offset, which
     * read_fs()/write_fs() pass straight through.  Every bound below is an
     * unsigned comparison that a negative value silently passes, after which
     * block_offset = (uint32_t)(offset % block_size) is huge and
     * memcpy(block_buf + block_offset, ...) lands BEFORE the kmalloc'd block
     * buffer -- an attacker-controlled write (or heap disclosure on read)
     * reachable from any unprivileged process via
     * lseek(fd, -100, SEEK_SET); write(fd, buf, 64). */
    if (offset < 0) {
        if (errp) *errp = -EINVAL;
        return 0;
    }
    ext2_fs_t *fs = node->fs;
    ext2_inode_t *inode = &node->inode;

    /* Extent-flagged inodes use a completely different on-disk
     * layout for i_block[] (inline ext4_extent_header + extents).
     * ext2_alloc_inode_block now dispatches to a small extent
     * allocator for append-only writes; sparse writes / multi-level
     * trees / splits / grows still hit -EROFS down inside that
     * allocator.  The legacy block-pointer code path below is
     * still safe to enter for extent inodes because it never
     * touches i_block[] — block lookups go through ext2_get_block_num
     * which already routes extent files to ext4_extent_resolve.  */

    mutex_lock(&node->lock);

    // Lazy allocate
    uint32_t block_size = fs->block_size;
    if (!node->block_buf) node->block_buf = kmalloc(block_size);
    if (!node->indirect_buf) node->indirect_buf = kmalloc(block_size);
    if (!node->dindirect_buf) node->dindirect_buf = kmalloc(block_size);
    if (!node->tindirect_buf) node->tindirect_buf = kmalloc(block_size);

    if (!node->block_buf || !node->indirect_buf || !node->dindirect_buf || !node->tindirect_buf) {
        mutex_unlock(&node->lock);
        if (errp) *errp = -ENOMEM;
        return 0;
    }

    uint8_t *block_buf = node->block_buf;
    uint32_t *indirect = node->indirect_buf;
    uint32_t *dindirect = node->dindirect_buf;
    uint32_t *tindirect = node->tindirect_buf;

    const uint8_t *buf = (const uint8_t *)buffer;
    uint32_t total_written = 0;

    while (size > 0) {
        uint32_t block_idx = (uint32_t)(offset / fs->block_size);
        uint32_t block_offset = (uint32_t)(offset % fs->block_size);
        uint32_t block_num = ext2_get_block_num(fs, inode, block_idx, indirect, dindirect, tindirect);

        // Allocate block if it doesn't exist
        if (block_num == 0) {
            if (ext2_alloc_inode_block(fs, inode, block_idx, indirect, dindirect, tindirect) != 0) {
                // Out of space
                if (errp) *errp = -ENOSPC;
                break;
            }
            block_num = ext2_get_block_num(fs, inode, block_idx, indirect, dindirect, tindirect);
            if (block_num == 0) {
                if (errp) *errp = -ENOSPC;
                break;
            }
            
            // Zero the newly allocated block
            memset(block_buf, 0, fs->block_size);
            ext2_write_block(fs, block_num, block_buf);
        }
        
        // Read block if we're doing a partial write
        if (block_offset != 0 || size < fs->block_size) {
            ext2_read_block(fs, block_num, block_buf);
        }
        
        uint32_t to_copy = fs->block_size - block_offset;
        if (to_copy > size) to_copy = size;
        
        memcpy(block_buf + block_offset, buf, to_copy);
        ext2_write_block(fs, block_num, block_buf);
        
        buf += to_copy;
        total_written += to_copy;
        offset += to_copy;
        size -= to_copy;
    }

    if (offset > inode->i_size) {
        inode->i_size = offset;
    }

    /* Update modification and change times */
    time_t now = get_time();
    inode->i_mtime = (uint32_t)now;
    inode->i_ctime = (uint32_t)now;
    
    mutex_unlock(&node->lock);
    return total_written;
}

// Allocate a node from the cache
/* Leak instrumentation — surfaced via `debug=vm_leak`.  Counts how
 * often the per-FS 64-slot node cache hits each path so we can tell
 * a stuck-pin condition from genuine cache pressure. */
uint64_t ext2_alloc_node_hits        = 0;  /* found cached by inode_num */
uint64_t ext2_alloc_node_new         = 0;  /* recycled an empty slot */
uint64_t ext2_alloc_node_fail        = 0;  /* every slot pinned or locked */
uint64_t ext2_alloc_node_fail_pinned = 0;  /* slots pinned at fail time */
uint64_t ext2_alloc_node_fail_locked = 0;  /* slots locked at fail time */

uint64_t ext2_finddir_calls          = 0;
uint64_t ext2_finddir_dcache_hit     = 0;
uint64_t ext2_finddir_walk_found     = 0;
uint64_t ext2_finddir_walk_missing   = 0;  /* finished walk without match */
uint64_t ext2_finddir_break_recv_malformed = 0;
uint64_t ext2_finddir_break_block0   = 0;

/* Root-pin watchdog: assert that the slot describing EXT2_ROOT_INO
 * keeps pin_count >= 1 across every alloc.  If it ever drops to 0
 * the slot becomes recyclable and `fs_root` becomes a dangling
 * pointer the next time anybody pulls a different inode through
 * ext2_alloc_node — this would explain the global lookup failure
 * we see after the first /bin walk. */
uint64_t ext2_root_pin_lost          = 0;

/*
 * Populate a cached fs_node from an inode: common fields plus the type-specific
 * flags/callbacks.  Does NOT touch ctx->pin_count, so it can be used both to
 * build a freshly recycled slot and to refresh a still-pinned slot whose inode
 * number was reallocated to a new (possibly different-type) file.
 */
static fs_node_t *ext2_node_build_from_inode(fs_node_t *node, ext2_node_t *ctx,
        ext2_inode_t *inode, uint32_t inode_num, ext2_fs_t *fs, int idx) {
    memset(node, 0, sizeof(fs_node_t));
    node->inode = inode_num;
    node->length = inode->i_size;
    node->mask = inode->i_mode & 0xFFF;
    node->uid = inode->i_uid;
    node->gid = inode->i_gid;
    node->atime = inode->i_atime;
    node->mtime = inode->i_mtime;
    node->ctime = inode->i_ctime;
    node->impl = (uintptr_t)ctx;
    node->mp = fs->mp;
    node->open = ext2_node_open;
    node->close = ext2_node_close;
    node->chmod = ext2_chmod;
    node->setattr = ext2_setattr;
    node->getattr = ext2_getattr;
    node->getxattr  = ext2_xattr_get;
    node->listxattr = ext2_xattr_list;
    ctx->cache_slot = (uint16_t)idx;

    uint16_t type = inode->i_mode & 0xF000;
    if (type == EXT2_S_IFDIR) {
        node->flags = FS_DIRECTORY;
        node->readdir = ext2_readdir;
        node->finddir = ext2_finddir;
        node->mkdir = ext2_mkdir;
        node->mknod = ext2_mknod;
        node->unlink = ext2_unlink;
        node->rmdir = ext2_rmdir;
        node->link = ext2_link;
        node->rename = ext2_rename;
        node->statfs = ext2_statfs;
        node->unmount = ext2_unmount;
        node->remount = ext2_remount;
        node->symlink = ext2_symlink;
    } else if (type == EXT2_S_IFREG) {
        node->flags = FS_FILE;
        node->read = ext2_file_read;
        node->write = ext2_file_write;
        node->truncate = ext2_truncate;
    } else if (type == EXT2_S_IFLNK) {
        node->flags = FS_SYMLINK;
        node->readlink = ext2_readlink;
    } else if (type == EXT2_S_IFCHR) {
        node->flags = FS_CHARDEVICE;
        node->rdev = inode->i_block[0];
    } else if (type == EXT2_S_IFBLK) {
        node->flags = FS_BLOCKDEVICE;
        node->rdev = inode->i_block[0];
    } else if (type == EXT2_S_IFIFO) {
        node->flags = FS_PIPE;
    } else if (type == EXT2_S_IFSOCK) {
        node->flags = FS_SOCKET;
    }
    return node;
}

fs_node_t *ext2_alloc_node(ext2_fs_t *fs, uint32_t inode_num, ext2_inode_t *inode) {
    int idx = -1;

    /* FS-01: serialize the whole select-or-populate sequence.  Slot lookup,
     * the pin_count==0 scan, and the memcpy() of the on-disk inode into the
     * chosen slot must be one atomic step, or two lookups race onto the same
     * slot.  Released on every return path below. */
    mutex_lock(&ext2_node_cache_lock);

    /* Pre-condition watchdog: scan for the slot holding the root
     * inode.  If we find it with pin_count==0 the slot is about to
     * become recyclable; ext2_mount() pinned it at boot and nothing
     * legitimately unpins it, so reaching 0 means somebody dropped
     * an extra close_fs() on a node we treated as root. */
    {
        for (int i = 0; i < EXT2_NODE_CACHE_SIZE; i++) {
            if (ext2_node_cache[i].fs == fs &&
                ext2_node_cache[i].inode_num == EXT2_ROOT_INO &&
                ext2_node_cache[i].pin_count == 0) {
                ext2_root_pin_lost++;
                if (ext2_root_pin_lost <= 4) {

                    kprintf("ext2: WARNING root slot %d pin_count=0 "
                            "(alloc_node entry, requesting inode=%u, "
                            "lost-pin event #%llu)\n",
                            i, inode_num,
                            (unsigned long long)ext2_root_pin_lost);
                }
                /* Re-pin defensively — never let the root slot fall
                 * to 0 even if something drops it.  This is a band-
                 * aid; the real fix lives wherever the extra close
                 * is coming from. */
                ext2_node_cache[i].pin_count = 1;
            }
        }
    }

    // 1. Search for existing node in cache
    for (int i = 0; i < EXT2_NODE_CACHE_SIZE; i++) {
        if (ext2_node_cache[i].fs == fs && ext2_node_cache[i].inode_num == inode_num) {
            /* Stale-slot guard: if the cached inode shows i_links_count==0
             * (deleted) but the disk inode the caller just read shows it's
             * live again, this slot was left behind by a prior unlink/rmdir
             * that couldn't evict because pin_count was non-zero (callers
             * that didn't take the orphan-deferred-delete path).  The inode
             * number has since been reallocated for a different file —
             * possibly a different file TYPE (e.g. directory → regular
             * file).  Returning this slot would hand back the old fs_node_t
             * with stale flags/callbacks (no `write` for the old directory),
             * which makes the new file unwritable.  Force the new-slot path
             * to recycle it. */
            if (ext2_node_cache[i].inode.i_links_count == 0 &&
                inode->i_links_count > 0) {
                if (ext2_node_cache[i].pin_count == 0) {
                    /* Unpinned: drop the slot and let the new-slot path below
                     * rebuild it cleanly. */
                    ext2_node_cache[i].fs = NULL;
                    ext2_node_cache[i].inode_num = 0;
                    memset(&ext2_fs_node_cache[i], 0,
                           sizeof(ext2_fs_node_cache[i]));
                    continue;
                }
                /* Still pinned by a lingering reference to the now-deleted
                 * inode (an unbalanced close).  We cannot free the slot, so
                 * refresh the cached inode and rebuild the fs_node's
                 * type-specific callbacks in place -- preserving pin_count --
                 * so the new file gets a valid `write` callback instead of
                 * handing back the old node and making writes fail EBADF.
                 * This was the cause of "cat: stdout: Bad file descriptor"
                 * floods when a configure recycled conftest.c/conftest.dir. */
                memcpy(&ext2_node_cache[i].inode, inode, sizeof(ext2_inode_t));
                ext2_alloc_node_hits++;
                fs_node_t *rebuilt = ext2_node_build_from_inode(&ext2_fs_node_cache[i],
                        &ext2_node_cache[i], inode, inode_num, fs, i);
                mutex_unlock(&ext2_node_cache_lock);
                return rebuilt;
            }
            ext2_alloc_node_hits++;
            mutex_unlock(&ext2_node_cache_lock);
            return &ext2_fs_node_cache[i];
        }
    }

    // 2. Allocate a new slot from the cache
    int start = ext2_node_cache_idx % EXT2_NODE_CACHE_SIZE;
    for (int i = 0; i < EXT2_NODE_CACHE_SIZE; i++) {
        int probe = (start + i) % EXT2_NODE_CACHE_SIZE;
        if (ext2_node_cache[probe].pin_count == 0 && ext2_node_cache[probe].lock.locked == 0) {
            idx = probe;
            ext2_node_cache_idx = (probe + 1) % EXT2_NODE_CACHE_SIZE;
            break;
        }
    }
    if (idx < 0) {
        unsigned pinned = 0, locked = 0;
        for (int i = 0; i < EXT2_NODE_CACHE_SIZE; i++) {
            if (ext2_node_cache[i].pin_count != 0) pinned++;
            if (ext2_node_cache[i].lock.locked != 0) locked++;
        }
        ext2_alloc_node_fail++;
        ext2_alloc_node_fail_pinned += pinned;
        ext2_alloc_node_fail_locked += locked;
        if (ext2_alloc_node_fail <= 4) {

            kprintf("ext2: alloc_node fail #%llu inode=%u "
                    "(pinned=%u locked=%u of %d)\n",
                    (unsigned long long)ext2_alloc_node_fail,
                    inode_num, pinned, locked, EXT2_NODE_CACHE_SIZE);
        }
        /* First failure: dump every pinned slot so the leak source
         * is visible (inode#, type, pin_count, orphaned flag).  The
         * fingerprint usually points right at the leak — e.g. dozens
         * of pin=1 on the same FS_FILE inode means an open-without-
         * close path, dozens of pin=N on /sbin/init's exec path
         * means proc_fork open_fs leaking into a non-balancing exit. */
        if (ext2_alloc_node_fail == 1) {
            kprintf("ext2: alloc_node fail snapshot (top pinned slots):\n");
            int shown = 0;
            for (int i = 0; i < EXT2_NODE_CACHE_SIZE && shown < 32; i++) {
                if (ext2_node_cache[i].pin_count == 0) continue;
                kprintf("  slot=%3d inode=%6u mode=%#06o pin=%u orphan=%u\n",
                        i,
                        ext2_node_cache[i].inode_num,
                        ext2_node_cache[i].inode.i_mode,
                        ext2_node_cache[i].pin_count,
                        ext2_node_cache[i].orphaned);
                shown++;
            }

            kprintf("ext2: open_fs=%lu close_fs=%lu delta=%ld\n",
                    fs_open_count, fs_close_count,
                    (long)(fs_open_count - fs_close_count));
        }
        mutex_unlock(&ext2_node_cache_lock);
        return NULL;
    }
    ext2_alloc_node_new++;
    
    ext2_node_t *ctx = &ext2_node_cache[idx];
    fs_node_t *node = &ext2_fs_node_cache[idx];
    
    // Clean up old context
    if (ctx->fs) {
        uint32_t old_block_size = ctx->fs->block_size;
        if (ctx->block_buf) { kfree(ctx->block_buf, old_block_size); ctx->block_buf = NULL; }
        if (ctx->indirect_buf) { kfree(ctx->indirect_buf, old_block_size); ctx->indirect_buf = NULL; }
        if (ctx->dindirect_buf) { kfree(ctx->dindirect_buf, old_block_size); ctx->dindirect_buf = NULL; }
        if (ctx->tindirect_buf) { kfree(ctx->tindirect_buf, old_block_size); ctx->tindirect_buf = NULL; }
    }

    memset(ctx, 0, sizeof(ext2_node_t));
    ctx->fs = fs;
    ctx->inode_num = inode_num;
    memcpy(&ctx->inode, inode, sizeof(ext2_inode_t));
    
    // Initialize lock for scratch buffers
    mutex_init(&ctx->lock, "ext2_node");

    // Initialize readdir cache (use -1 to indicate uninitialized)
    ctx->last_readdir_idx = (uint64_t)-1;
    ctx->last_readdir_pos = 0;

    // Initialize dcache
    ctx->dcache_idx = 0;
    memset(ctx->dcache, 0, sizeof(ctx->dcache));

    ctx->pin_count = 0;
    fs_node_t *built = ext2_node_build_from_inode(node, ctx, inode, inode_num, fs, idx);
    mutex_unlock(&ext2_node_cache_lock);
    return built;
}

static int ext2_chmod(fs_node_t *node, uint32_t mode) {
    ext2_node_t *ctx;

    if (!node) return -EINVAL;

    ctx = (ext2_node_t *)(uintptr_t)node->impl;
    if (!ctx || !ctx->fs) return -EINVAL;
    if (EXT2_RO_REFUSE(ctx->fs)) return -EROFS;

    ctx->inode.i_mode = (uint16_t)((ctx->inode.i_mode & 0xF000U) | (mode & 0x0FFFU));
    ctx->inode.i_ctime = (uint32_t)node->ctime;

    if (ext2_write_inode(ctx->fs, ctx->inode_num, &ctx->inode) != 0) {
        return -EIO;
    }

    return 0;
}

/* ext2_setattr — write fs_attr's mask-selected fields into the
 * on-disk inode and flush.  Mirrors the change into the cached
 * fs_node fields so a follow-up stat() picks it up without a
 * fresh inode-read.  ext2's 32-bit timestamps clamp to 2038-01-19
 * — substrate's int64_t time_t wraps when cast.  Fixing Y2038 in
 * ext2 means moving to ext4-style 64-bit timestamps; out of scope
 * for this commit.  */
static int ext2_setattr(fs_node_t *node, const struct fs_attr *a) {
    if (!node || !a) return -EINVAL;
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    if (!ctx || !ctx->fs) return -EINVAL;
    if (EXT2_RO_REFUSE(ctx->fs)) return -EROFS;
    if (a->mask == 0) return 0;

    /* SIZE — truncation needs to free data blocks; route through the existing
     * truncate path.  EXT2-19: this refusal used to sit at the *end* of the
     * function, after every other selected field had already been written into
     * ctx->inode and the cached fs_node.  The call returned -EINVAL without
     * reaching ext2_write_inode(), so the caller saw a clean rejection while
     * the in-core inode had silently changed -- and the next unrelated write
     * to that inode committed the mutation to disk.  Reject before touching
     * anything. */
    if (a->mask & FS_ATTR_SIZE) return -EINVAL;

    /* Whether the on-disk inode has room for nsec extras depends on
     * the mount-wide inode_size (256 vs 128 bytes).  ext2_write_inode
     * already gates the write byte count on inode_size, so writing
     * the *_extra fields on a 128-byte mount is harmless (the bytes
     * just don't reach disk).  We still set them in the cached copy
     * so a subsequent getattr returns what the caller asked for.  */
    int big_inode = (ctx->fs->inode_size > EXT2_GOOD_OLD_INODE_SIZE);
    if (a->mask & FS_ATTR_ATIME) {
        ctx->inode.i_atime = (uint32_t)a->atime;
        if (big_inode)
            ctx->inode.i_atime_extra = ext2_time_pack_extra(a->atime_nsec);
        node->atime        = a->atime;
    }
    if (a->mask & FS_ATTR_MTIME) {
        ctx->inode.i_mtime = (uint32_t)a->mtime;
        if (big_inode)
            ctx->inode.i_mtime_extra = ext2_time_pack_extra(a->mtime_nsec);
        node->mtime        = a->mtime;
    }
    if (a->mask & FS_ATTR_CTIME) {
        ctx->inode.i_ctime = (uint32_t)a->ctime;
        if (big_inode)
            ctx->inode.i_ctime_extra = ext2_time_pack_extra(a->ctime_nsec);
        node->ctime        = a->ctime;
    }
    if (a->mask & FS_ATTR_MODE) {
        ctx->inode.i_mode = (uint16_t)((ctx->inode.i_mode & 0xF000U) |
                                       (a->mode & 0x0FFFU));
        node->mask        = a->mode & 0x0FFFU;
    }
    if (a->mask & FS_ATTR_UID) {
        ctx->inode.i_uid = (uint16_t)a->uid;
        node->uid        = a->uid;
    }
    if (a->mask & FS_ATTR_GID) {
        ctx->inode.i_gid = (uint16_t)a->gid;
        node->gid        = a->gid;
    }
    if (ext2_write_inode(ctx->fs, ctx->inode_num, &ctx->inode) != 0)
        return -EIO;
    return 0;
}

/* ext2_getattr — fill from the cached on-disk inode.  Same shape
 * as the VFS-generic fallback but going through the explicit op
 * makes a future "validate cached inode against disk before
 * answering" hook trivial.  */
static int ext2_getattr(fs_node_t *node, struct fs_attr *a) {
    if (!node || !a) return -EINVAL;
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    if (!ctx) return -EINVAL;
    a->mask  = FS_ATTR_ATIME | FS_ATTR_MTIME | FS_ATTR_CTIME |
               FS_ATTR_MODE  | FS_ATTR_UID   | FS_ATTR_GID   |
               FS_ATTR_SIZE;
    a->atime = ctx->inode.i_atime;
    a->mtime = ctx->inode.i_mtime;
    a->ctime = ctx->inode.i_ctime;
    if (ctx->fs->inode_size > EXT2_GOOD_OLD_INODE_SIZE) {
        a->atime_nsec = ext2_time_extra_nsec(ctx->inode.i_atime_extra);
        a->mtime_nsec = ext2_time_extra_nsec(ctx->inode.i_mtime_extra);
        a->ctime_nsec = ext2_time_extra_nsec(ctx->inode.i_ctime_extra);
    } else {
        a->atime_nsec = a->mtime_nsec = a->ctime_nsec = 0;
    }
    a->mode  = ctx->inode.i_mode & 0x0FFFU;
    a->uid   = ctx->inode.i_uid;
    a->gid   = ctx->inode.i_gid;
    a->size  = ctx->inode.i_size;
    return 0;
}

// Read symlink target
int ext2_readlink(fs_node_t *node, char *buf, size_t size) {
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    ext2_inode_t *inode = &ctx->inode;

    /* A symlink target must fit in the caller's buffer with room for
     * the NUL terminator.  Both inode->i_size (untrusted on-disk
     * metadata) and `size` are bound here without any addition that
     * could overflow: the previous `size < link_size + 1` test wrapped
     * to 0 when i_size was 0xFFFFFFFF, leaving link_size enormous and
     * driving a ~4 GiB ext2_inode_read into buf plus a wild
     * buf[link_size] NUL write.  A 0-length caller buffer leaves no
     * room even for the terminator. */
    if (size == 0) return 0;

    /* Cap an absurd on-disk symlink length.  A valid symlink target is
     * at most one filesystem block (PATH_MAX-class); anything larger is
     * corrupt.  Then bound to the caller's buffer (leaving room for the
     * NUL).  Both clamps are subtractions/comparisons only — no
     * additions that could wrap. */
    uint32_t link_size = inode->i_size;
    if (ctx->fs && link_size > ctx->fs->block_size)
        link_size = ctx->fs->block_size;
    if (link_size >= size) link_size = (uint32_t)(size - 1);

    // Fast symlink: if i_size <= 60, target is stored in i_block[]
    if (inode->i_size <= 60) {
        /* Clamp to sizeof(i_block) to prevent overflow (finding #26) */
        if (link_size > 60) link_size = 60;
        memcpy(buf, (char *)inode->i_block, link_size);
    } else {
        // Slow symlink: target is in data blocks
        ext2_inode_read(ctx, 0, link_size, (uint8_t *)buf);
    }
    buf[link_size] = '\0';
    return link_size;
}

// File read operation
size_t ext2_file_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    return ext2_inode_read(ctx, offset, size, buffer);
}

// File write operation
size_t ext2_file_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    ext2_fs_t *fs = ctx->fs;
    if (EXT2_RO_REFUSE(fs)) return (size_t)-EROFS;

    /*
     * EXT2-12: i_size is a 32-bit field and this driver never touches
     * i_size_high, but ext2_inode_write() assigns a 64-bit off_t straight
     * into it.  A write past 4 GiB therefore landed on disk correctly and
     * then recorded the length modulo 2^32 -- the file came back looking
     * a few bytes long with gigabytes of orphaned blocks attached.
     * ext2_truncate() already refuses to cross the same boundary with
     * -EFBIG; do the same here rather than corrupt the length silently.
     */
    if (offset >= 0 &&
        (uint64_t)offset + (uint64_t)size > 0xFFFFFFFFULL)
        return (size_t)-EFBIG;

    /* Extent-tree files: now go through the partial extent-write
     * path inside ext2_inode_write -> ext4_extent_alloc_inode_block.
     * Append-only (sparse / multi-level still bail with -EROFS
     * inside the allocator) so a write that "succeeds" up to N
     * bytes and then fails will leave a consistent file at length
     * N.  */
    int werr = 0;
    uint32_t written = ext2_inode_write(ctx, offset, size, buffer, &werr);

    // Write updated inode back to disk
    if (written > 0) {
        ext2_write_inode(fs, ctx->inode_num, &ctx->inode);
        node->length = ctx->inode.i_size; // Update VFS node size
    }

    /* EXT2-15: a partial write is a success -- the caller retries from where
     * we stopped.  Writing *nothing* when something was asked for is not: it
     * has to surface as an error, or write(2) returns 0 and the caller loops
     * forever on a full filesystem. */
    if (written == 0 && size > 0)
        return (size_t)(werr ? werr : -ENOSPC);

    return written;
}

// Read directory entry at index
struct dirent *ext2_readdir(fs_node_t *node, uint64_t index) {
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    ext2_fs_t *fs = ctx->fs;

    // Lazily set mount pointer in filesystem context
    if (!fs->mp && node->mp) fs->mp = node->mp;
    
    // Read the directory data.
    //
    // `index` is an opaque BYTE OFFSET into the directory file, not an entry
    // ordinal (see the d_off cursor threading in kern_getdents).  Byte offsets
    // are stable across deletion: ext2 unlink only folds a removed record's
    // space into its predecessor's rec_len, it never moves a surviving entry,
    // so resuming at a saved offset can never skip a live name.  The previous
    // entry-index scheme recounted only live entries on every call, so a
    // reader that interleaved unlink() (rm -rf) renumbered the survivors and
    // silently skipped a batch each pass -- the "rm -rf needs several runs" bug.
    uint32_t dir_size = ctx->inode.i_size;
    uint32_t pos = (uint32_t)index;

    mutex_lock(&ctx->lock);

    // Lazy allocate
    uint32_t block_size = fs->block_size;
    if (!ctx->block_buf) ctx->block_buf = kmalloc(block_size);
    if (!ctx->indirect_buf) ctx->indirect_buf = kmalloc(block_size);
    if (!ctx->dindirect_buf) ctx->dindirect_buf = kmalloc(block_size);
    if (!ctx->tindirect_buf) ctx->tindirect_buf = kmalloc(block_size);

    if (!ctx->block_buf || !ctx->indirect_buf || !ctx->dindirect_buf || !ctx->tindirect_buf) {
        mutex_unlock(&ctx->lock);
        return NULL;
    }

    uint8_t *ext2_dir_buf = ctx->block_buf;
    uint32_t *indirect = ctx->indirect_buf;
    uint32_t *dindirect = ctx->dindirect_buf;
    uint32_t *tindirect = ctx->tindirect_buf;

    struct dirent *result = NULL;

    while (pos < dir_size) {
        // Read block containing current position
        uint32_t block_idx = pos / fs->block_size;
        uint32_t block_off = pos % fs->block_size;
        uint32_t block_num = ext2_get_block_num(fs, &ctx->inode, block_idx, indirect, dindirect, tindirect);
        
        if (block_num == 0) break;
        ext2_read_block(fs, block_num, ext2_dir_buf);
        
        // Parse entries in this block
        while (block_off + 8 <= fs->block_size && pos < dir_size) {
            ext2_dirent_t *de = (ext2_dirent_t *)(ext2_dir_buf + block_off);
            
            if (de->rec_len < 8 || block_off + de->rec_len > fs->block_size) break;

            // Return the first live entry at or after `pos`; deleted records
            // (inode == 0) are walked over.  d_off is the byte offset of the
            // NEXT record, which the caller hands back on the following call.
            if (de->inode != 0 && de->name_len > 0) {
                ctx->current_dirent.d_ino = de->inode;
                uint32_t len = de->name_len;
                /* Clamp to what actually fits within this record (the name
                 * starts at offset 8) so a bogus on-disk name_len can't read
                 * past the directory block into adjacent kernel memory. */
                if (len > (uint32_t)(de->rec_len - 8)) {
                    len = de->rec_len - 8;
                }
                // Ensure name fits in the dirent buffer.
                if (len >= sizeof(ctx->current_dirent.d_name)) {
                    len = sizeof(ctx->current_dirent.d_name) - 1;
                }
                memcpy(ctx->current_dirent.d_name, de->name, len);
                ctx->current_dirent.d_name[len] = '\0';
                ctx->current_dirent.d_namlen = (uint8_t)len;
                ctx->current_dirent.d_type = ext2_file_type_to_dt(de->file_type);
                ctx->current_dirent.d_reclen = (uint16_t)(((8 + len + 1 + 3) / 4) * 4);
                ctx->current_dirent.d_off = pos + de->rec_len;
                result = &ctx->current_dirent;
                goto cleanup;
            }

            if (de->rec_len == 0) break; // Prevent infinite loop
            block_off += de->rec_len;
            pos += de->rec_len;
        }

        /* Same guard ext2_add_entry() carries: the inner loop `break`s on a
         * malformed record (rec_len < 8, or one running past the block) WITHOUT
         * advancing pos.  The outer loop then recomputes the identical
         * block_idx, re-reads the same block, breaks again -- an unkillable
         * 100%-CPU kernel spin, here with ctx->lock held so every other user of
         * this directory blocks behind it.  Skip to the next block instead. */
        uint32_t block_end = (block_idx + 1) * fs->block_size;
        if (pos < block_end)
            pos = block_end;
    }

cleanup:
    mutex_unlock(&ctx->lock);
    return result;
}

/* Counters for htree path; mostly diagnostic, surface them via
 * the same dump as the other finddir counters.  */
uint64_t ext2_finddir_htree_hit  = 0;
uint64_t ext2_finddir_htree_miss = 0;
uint64_t ext2_finddir_htree_bypass = 0;

/* Search one leaf block (already read into `block`, of size
 * fs->block_size) for `name`.  Returns inode number on hit, 0 on
 * miss / malformed.  Caller holds ctx->lock.  */
static uint32_t ext2_scan_leaf(uint8_t *block, uint32_t block_size,
                               const char *name, size_t name_len) {
    uint32_t off = 0;
    while (off + 8 <= block_size) {
        ext2_dirent_t *de = (ext2_dirent_t *)(block + off);
        if (de->rec_len < 8 || off + de->rec_len > block_size) return 0;
/* de->name_len is on-disk and unbounded; only rec_len was checked against the
 * block.  A last-in-block record with rec_len 8 and name_len 200 made the
 * memcmp below read 200 bytes past the kmalloc'd directory block.  The name
 * has to fit in the record, after the 8-byte header.  (ext2_readdir already
 * clamps this way; these three comparison sites did not.) */
        if (de->inode != 0 && de->rec_len >= 8 &&
            de->name_len <= (uint32_t)(de->rec_len - 8) &&
            de->name_len == name_len &&
            memcmp(de->name, name, name_len) == 0) {
            return de->inode;
        }
        off += de->rec_len;
    }
    return 0;
}

/* htree-accelerated lookup.  Returns:
 *   >0  : inode number of the matching entry
 *    0  : name proven absent (full leaf walked, no hit)
 *   -1  : not an htree directory / hash version unknown / block 0
 *         absent — caller must fall back to linear scan
 *
 * Limitations vs FreeBSD:
 *   - single-level only (h_ind_levels == 0).  Multi-level dirs are
 *     rare in practice (would need >~120 leaf blocks at h_ind_levels=0;
 *     ~3M entries before that fills) but if we see h_ind_levels > 0
 *     we return -1 so the linear-scan fallback covers the case.
 *   - we only check the one leaf the htree points to.  If a name
 *     was inserted in a hash-collision-overflow leaf we'd return
 *     0 here; the caller treats 0 as a real miss.  Acceptable for
 *     a first cut — the same risk doesn't apply on lookup because
 *     the hash for that name lands in the same leaf as the actual
 *     entry under any sane indexer.
 */
static int ext2_htree_lookup(ext2_node_t *ctx, const char *name,
                             size_t name_len, uint32_t *out_inode) {
    ext2_fs_t *fs = ctx->fs;
    if (!(fs->sb.s_feature_compat & EXT2F_COMPAT_DIRHASHINDEX)) return -1;
    if (!(ctx->inode.i_flags & EXT2_INDEX_FL))                  return -1;

    uint8_t *root = ctx->block_buf;     /* scratch — already kmalloc'd by caller */
    /* Root block is block 0 of the directory.  Walk the indirect/
     * direct map to find its physical address.  */
    uint32_t root_blk = ext2_get_block_num(fs, &ctx->inode, 0,
                                           ctx->indirect_buf,
                                           ctx->dindirect_buf,
                                           ctx->tindirect_buf);
    if (root_blk == 0) return -1;
    if (ext2_read_block(fs, root_blk, root) != fs->block_size) return -1;

    /* htree info starts at byte 24 (past 12-byte "." + 12-byte "..").
     *   root+24: h_reserved1 (4B)
     *   root+28: h_hash_version (1B)
     *   root+29: h_info_len    (1B, typically 8)
     *   root+30: h_ind_levels  (1B)
     *   root+31: h_reserved2   (1B)
     *
     * Index entries begin at root + 24 + h_info_len.  The FIRST
     * entry slot is overloaded as a count:
     *   bytes 0-1: h_entries_max
     *   bytes 2-3: h_entries_num   (total slots in use, INCLUDING
     *                                this leftmost-leaf slot)
     *   bytes 4-7: h_blk           (the leftmost leaf's block #)
     * Real index entries are slots [1..h_entries_num-1], each
     * { u32 h_hash; u32 h_blk }.  */
    if (fs->block_size < 64) return -1;
    uint8_t  hash_version = root[24 + 4];
    uint8_t  h_info_len   = root[24 + 5];
    uint8_t  ind_levels   = root[24 + 6];
    if (ind_levels != 0) return -1;     /* multi-level — fall back  */
    if (h_info_len < 8 || h_info_len > 16) return -1;
    uint32_t entries_off  = 24 + h_info_len;
    if (entries_off + 8 > fs->block_size) return -1;
    uint16_t entries_num  = *(uint16_t *)(root + entries_off + 2);
    if (entries_num == 0 ||
        entries_off + (uint32_t)entries_num * 8 > fs->block_size)
        return -1;

    uint32_t hash_major = 0, hash_minor = 0;
    if (ext2_htree_hash(name, (int)name_len, fs->hash_seed,
                        (int)hash_version, &hash_major, &hash_minor) != 0)
        return -1;

    /* Binary search entries[1..entries_num-1] for the largest
     * entry whose h_hash <= hash_major.  If hash_major is below
     * every real entry's h_hash, found == 0 (= the leftmost leaf
     * slot).  */
    uint8_t *entries = root + entries_off;
    int start = 1, end = (int)entries_num - 1;
    while (start <= end) {
        int mid = start + (end - start) / 2;
        uint32_t mh = *(uint32_t *)(entries + mid * 8);
        if (mh > hash_major) end = mid - 1;
        else                 start = mid + 1;
    }
    int found = start - 1;
    uint32_t leaf_logical = *(uint32_t *)(entries + found * 8 + 4);

    /* Translate logical block in directory to physical, then read. */
    uint32_t leaf_phys = ext2_get_block_num(fs, &ctx->inode, leaf_logical,
                                            ctx->indirect_buf,
                                            ctx->dindirect_buf,
                                            ctx->tindirect_buf);
    if (leaf_phys == 0) return -1;
    if (ext2_read_block(fs, leaf_phys, root) != fs->block_size) return -1;

    uint32_t inum = ext2_scan_leaf(root, fs->block_size, name, name_len);
    if (inum != 0) {
        *out_inode = inum;
        ext2_finddir_htree_hit++;
        return 1;
    }
    ext2_finddir_htree_miss++;
    return 0;
}

// Find entry by name in directory
fs_node_t *ext2_finddir(fs_node_t *node, char *name) {
    if (!node || !name) return NULL;

    ext2_finddir_calls++;
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    ext2_fs_t *fs = ctx->fs;

    // Lazily set mount pointer in filesystem context
    if (!fs->mp && node->mp) fs->mp = node->mp;

    size_t name_len = strlen(name);
    uint32_t dir_size = ctx->inode.i_size;
    uint32_t pos = 0;

    /* FS-05: pin this directory's own cache slot for the duration of the
     * walk.  The ext2_alloc_node() call we make for a matching child scans
     * for a pin_count==0 slot to recycle, and would otherwise be free to
     * recycle THIS slot out from under us mid-lookup (the parent is not
     * necessarily pinned by the caller).  Balanced at the cleanup label.
     * EXT2-18: under ext2_node_cache_lock, the lock the recycling scan
     * itself holds -- an unsynchronised bump can be lost against a
     * concurrent close and leave the slot recyclable mid-walk. */
    mutex_lock(&ext2_node_cache_lock);
    ctx->pin_count++;
    mutex_unlock(&ext2_node_cache_lock);

    mutex_lock(&ctx->lock);

    fs_node_t *result_node = NULL;
    int walk_break_malformed = 0;
    int walk_break_block0 = 0;

    // 1. Check dcache
    for (int k = 0; k < EXT2_DCACHE_SIZE; k++) {
        if (ctx->dcache[k].inode_num != 0 &&
            ctx->dcache[k].name_len == name_len &&
            memcmp(ctx->dcache[k].name, name, name_len) == 0) {

            /* Negative (known-absent) entry: the name is cached as not present
             * in this directory, so skip the full linear scan and report a
             * miss immediately. */
            if (ctx->dcache[k].inode_num == EXT2_DCACHE_NEGATIVE) {
                ext2_finddir_dcache_hit++;
                result_node = NULL;
                goto cleanup;
            }

            ext2_inode_t inode;
            if (ext2_read_inode(fs, ctx->dcache[k].inode_num, &inode) == 0) {
                result_node = ext2_alloc_node(fs, ctx->dcache[k].inode_num, &inode);
                if (!result_node) goto cleanup;
                // Copy name
                size_t len = name_len;
                if (len >= sizeof(result_node->name)) {
                    len = sizeof(result_node->name) - 1;
                }
                memcpy(result_node->name, name, len);
                result_node->name[len] = '\0';
                ext2_finddir_dcache_hit++;
                goto cleanup;
            }
        }
    }

    // Lazy allocate
    uint32_t block_size = fs->block_size;
    if (!ctx->block_buf) ctx->block_buf = kmalloc(block_size);
    if (!ctx->indirect_buf) ctx->indirect_buf = kmalloc(block_size);
    if (!ctx->dindirect_buf) ctx->dindirect_buf = kmalloc(block_size);
    if (!ctx->tindirect_buf) ctx->tindirect_buf = kmalloc(block_size);

    if (!ctx->block_buf || !ctx->indirect_buf || !ctx->dindirect_buf || !ctx->tindirect_buf) {
        result_node = NULL;
        goto cleanup;
    }

    uint8_t *ext2_dir_buf = ctx->block_buf;
    uint32_t *indirect = ctx->indirect_buf;
    uint32_t *dindirect = ctx->dindirect_buf;
    uint32_t *tindirect = ctx->tindirect_buf;

    /* htree fast path.  Only attempted when the dir actually carries
     * the EXT2_INDEX_FL flag — most dirs in real filesystems do
     * (mkfs.ext4 turns it on automatically as soon as a directory
     * needs > 1 block).  A return of >=0 is authoritative:
     *   >0 -> found, build node and exit;
     *    0 -> not found in the indexed leaf (treat as miss);
     *   -1 -> can't htree (single-block dir / unknown hash version
     *         / multi-level index we don't support yet), fall through
     *         to linear scan.  */
    {
        uint32_t htree_inum = 0;
        int htr = ext2_htree_lookup(ctx, name, name_len, &htree_inum);
        if (htr > 0) {
            ext2_inode_t inode;
            if (ext2_read_inode(fs, htree_inum, &inode) == 0) {
                result_node = ext2_alloc_node(fs, htree_inum, &inode);
                if (result_node) {
                    size_t len = name_len;
                    if (len >= sizeof(result_node->name))
                        len = sizeof(result_node->name) - 1;
                    memcpy(result_node->name, name, len);
                    result_node->name[len] = '\0';
                    goto cleanup;
                }
            }
        } else if (htr == 0) {
            /* htree said "not here" — trust the indexer and return
             * NULL.  This is the FreeBSD behaviour; linear-scanning
             * after a successful htree miss is a waste.  */
            goto cleanup;
        }
        if (htr < 0) ext2_finddir_htree_bypass++;
        /* fall through to linear scan */
    }

    while (pos < dir_size) {
        uint32_t block_idx = pos / fs->block_size;
        uint32_t block_off = pos % fs->block_size;
        uint32_t block_num = ext2_get_block_num(fs, &ctx->inode, block_idx, indirect, dindirect, tindirect);

        if (block_num == 0) { walk_break_block0 = 1; break; }
        ext2_read_block(fs, block_num, ext2_dir_buf);

        while (block_off + 8 <= fs->block_size && pos < dir_size) {
            ext2_dirent_t *de = (ext2_dirent_t *)(ext2_dir_buf + block_off);

            if (de->rec_len < 8 || block_off + de->rec_len > fs->block_size) {
                walk_break_malformed = 1;
                /* Round pos up to next block boundary so the outer
                 * loop doesn't re-read the same garbage in a tight
                 * spin. */
                pos = (block_idx + 1) * fs->block_size;
                break;
            }

            if (de->inode != 0 && de->name_len > 0) {
                // Compare names
                if (de->rec_len >= 8 &&
                    de->name_len <= (uint32_t)(de->rec_len - 8) &&
                    de->name_len == name_len &&
                    memcmp(de->name, name, de->name_len) == 0) {
                    // Found it - read the inode and return a node
                    ext2_inode_t inode;
                    if (ext2_read_inode(fs, de->inode, &inode) == 0) {
                        result_node = ext2_alloc_node(fs, de->inode, &inode);
                        if (!result_node) goto cleanup;
                        // Copy name
                        uint32_t len = de->name_len;
                        if (len >= sizeof(result_node->name)) {
                            len = sizeof(result_node->name) - 1;
                        }
                        memcpy(result_node->name, de->name, len);
                        result_node->name[len] = '\0';

                        // Add to dcache only if name fits
                        if (len < sizeof(ctx->dcache[0].name)) {
                            uint32_t idx = ctx->dcache_idx++ % EXT2_DCACHE_SIZE;
                            ctx->dcache[idx].inode_num = de->inode;
                            ctx->dcache[idx].name_len = len;
                            memcpy(ctx->dcache[idx].name, de->name, len);
                            ctx->dcache[idx].name[len] = '\0';
                        }

                        ext2_finddir_walk_found++;
                        goto cleanup;
                    }
                }
            }

            if (de->rec_len == 0) {
                walk_break_malformed = 1;
                pos = (block_idx + 1) * fs->block_size;
                break;
            }
            block_off += de->rec_len;
            pos += de->rec_len;
        }
    }

    /* Walk completed without a match.  Record any abnormal break
     * paths so the proc_exit dump shows whether the directory
     * iteration is silently truncating. */
    if (!result_node) {
        ext2_finddir_walk_missing++;
        if (walk_break_malformed) ext2_finddir_break_recv_malformed++;
        if (walk_break_block0)    ext2_finddir_break_block0++;
        /* Cache the negative result so a repeated lookup of this absent name
         * (linker/PATH/perso searching multiple dirs) skips the linear scan.
         * Only for a clean, complete walk — a truncated/malformed walk may be
         * hiding a real entry, so we must not record it as absent. */
        if (!walk_break_malformed && !walk_break_block0 &&
            name_len < sizeof(ctx->dcache[0].name)) {
            uint32_t idx = ctx->dcache_idx++ % EXT2_DCACHE_SIZE;
            ctx->dcache[idx].inode_num = EXT2_DCACHE_NEGATIVE;
            ctx->dcache[idx].name_len = (uint8_t)name_len;
            memcpy(ctx->dcache[idx].name, name, name_len);
            ctx->dcache[idx].name[name_len] = '\0';
        }
        if ((walk_break_malformed || walk_break_block0) &&
            (ext2_finddir_break_recv_malformed +
             ext2_finddir_break_block0) <= 4) {

            kprintf("ext2: finddir(inode=%u, '%s') walk truncated: "
                    "malformed=%d block0=%d pos=%u dir_size=%u\n",
                    ctx->inode_num, name,
                    walk_break_malformed, walk_break_block0,
                    pos, dir_size);
        }
    }

cleanup:
    mutex_unlock(&ctx->lock);
    /* FS-05: release the parent pin taken on entry.  Done after dropping
     * ctx->lock so the slot is fully quiescent; the returned child node is
     * a distinct slot and is unaffected. */
    mutex_lock(&ext2_node_cache_lock);
    if (ctx->pin_count > 0) ctx->pin_count--;
    mutex_unlock(&ext2_node_cache_lock);
    return result_node;
}

// Mount ext2 filesystem
fs_node_t *ext2_mount(const char *device, uint32_t flags, void *data) {
    (void)device;

    fs_node_t *dev = (fs_node_t *)data;
    if (!dev || !dev->read) {
        kprint("EXT2: No device or read function\n");
        return NULL;
    }

    ext2_fs_t *fs = kmalloc(sizeof(ext2_fs_t));
    if (!fs) return NULL;
    memset(fs, 0, sizeof(ext2_fs_t));
    fs->mnt_flags = flags;
    fs->readonly  = !!(flags & MNT_RDONLY);

    // Read superblock (at offset 1024)
    uint8_t sb_buf[1024];
    uint32_t read = dev->read(dev, 1024, 1024, sb_buf);
    if (read != 1024) {
        kprint("EXT2: Failed to read superblock\n");
        kfree(fs, sizeof(ext2_fs_t));
        return NULL;
    }
    
    memcpy(&fs->sb, sb_buf, sizeof(ext2_superblock_t));
    
    if (fs->sb.s_magic != EXT2_SUPER_MAGIC) {
        kprint("EXT2: Invalid magic number\n");
        kfree(fs, sizeof(ext2_fs_t));
        return NULL;
    }

    /* Feature-flag gate.  Refuse mounts when the filesystem
     * advertises an INCOMPAT bit we don't understand (writing
     * blindly would corrupt structures we can't parse).  For
     * unsupported ROCOMPAT bits, we'd want to force a ro-mount;
     * substrate doesn't have a read-only mount flag yet, so we
     * just warn and proceed — a future commit should plumb
     * MS_RDONLY through and refuse rw mounts here.  COMPAT bits
     * never block.  */
    if (fs->sb.s_rev_level >= 1) {
        uint32_t bad_inc = fs->sb.s_feature_incompat & ~EXT2F_INCOMPAT_SUPP;
        if (bad_inc) {

            kprintf("ext2: mount refused — unsupported INCOMPAT features 0x%08x\n", bad_inc);
            if (bad_inc & EXT2F_INCOMPAT_JOURNAL_DEV)
                kprintf("ext2:   journal device (ext3+ external journal)\n");
            if (bad_inc & EXT2F_INCOMPAT_RECOVER)
                kprintf("ext2:   needs journal replay — won't mount dirty ext3/4\n");
            if (bad_inc & EXT2F_INCOMPAT_64BIT)
                kprintf("ext2:   64-bit block addresses\n");
            if (bad_inc & EXT2F_INCOMPAT_MMP)
                kprintf("ext2:   multi-mount protection\n");
            if (bad_inc & EXT2F_INCOMPAT_CSUM_SEED)
                kprintf("ext2:   metadata csum seed\n");
            kfree(fs, sizeof(ext2_fs_t));
            return NULL;
        }
        uint32_t bad_ro = fs->sb.s_feature_ro_compat & ~EXT2F_ROCOMPAT_SUPP;
        if (bad_ro) {

            kprintf("ext2: mount forced read-only — unsupported ROCOMPAT 0x%08x\n",
                    bad_ro);
            fs->readonly = 1;
            fs->force_readonly = 1;   /* rw remount must be refused */
        }
        /* Informational: log which ext3/ext4 features are present.  */
        if (fs->sb.s_feature_compat & EXT2F_COMPAT_HASJOURNAL) {

            kprintf("ext2: ext3+ journal present (ignored — read-only journal use)\n");
        }
        if (fs->sb.s_feature_incompat & EXT2F_INCOMPAT_EXTENTS) {

            kprintf("ext2: ext4 extents enabled — extent-tree files supported\n");
        }

        /* metadata_csum verify on the superblock.  The on-disk
         * superblock has the csum at offset 0x3FC (1020); CRC32C
         * over the first 1020 bytes must equal that value.  If
         * mismatch, refuse the mount — silently mounting a
         * checksum-mismatched superblock invites later corruption
         * (we'd start writing csum-claiming-valid blocks whose
         * derived csums use stale superblock state).  */
        if (fs->sb.s_feature_ro_compat & EXT2F_ROCOMPAT_METADATA_CKSUM) {

            uint32_t want = *(uint32_t *)(sb_buf + 0x3FC);
            uint32_t got  = crc32c_update(0xFFFFFFFFu, sb_buf, 0x3FC);
            if (want != got) {
                kprintf("ext2: superblock checksum mismatch want=%08x got=%08x — refuse mount\n",
                        want, got);
                kfree(fs, sizeof(ext2_fs_t));
                return NULL;
            }
            kprintf("ext2: superblock metadata_csum verified (%08x)\n", got);
        }

        /* Stash htree hash seed (sb_buf+236, four u32 words).  Used
         * by ext2_htree_hash() — initial state for the half_md4 and
         * tea hash functions.  Always loaded if rev_level >= 1
         * because mkfs.ext4 generates a non-zero seed even when
         * neither metadata_csum nor htree-by-default is requested.  */
        memcpy(fs->hash_seed, sb_buf + 236, sizeof(fs->hash_seed));

        if (fs->sb.s_feature_ro_compat & EXT2F_ROCOMPAT_METADATA_CKSUM) {

            /* Establish the csum seed used for every other csum'd
             * object on the filesystem.  Default = crc32c(~0, uuid).
             * If the fs has INCOMPAT_CSUM_SEED set, the seed is the
             * explicit s_checksum_seed superblock word (offset 0x270)
             * instead — this lets fsck change the UUID without
             * recomputing every csum on disk.  */
            if (fs->sb.s_feature_incompat & EXT2F_INCOMPAT_CSUM_SEED) {
                fs->csum_seed = *(uint32_t *)(sb_buf + 0x270);
            } else {
                fs->csum_seed = crc32c_update(0xFFFFFFFFu, fs->sb.s_uuid,
                                              sizeof(fs->sb.s_uuid));
            }
        }
    }

    fs->device = dev;

    // Validate s_log_block_size before shifting (max 64KB blocks, i.e. log=6)
    if (fs->sb.s_log_block_size > 6) {
        kprint("EXT2: Invalid s_log_block_size\n");
        kfree(fs, sizeof(ext2_fs_t));
        return NULL;
    }
    fs->block_size = 1024 << fs->sb.s_log_block_size;

    fs->inodes_per_group = fs->sb.s_inodes_per_group;
    fs->blocks_per_group = fs->sb.s_blocks_per_group;
    /* The block bitmap for a group is a single block, so it holds at most
     * block_size*8 bits — one per block in the group.  A blocks_per_group
     * larger than that (attacker-controlled on-disk value) makes the free-block
     * scan walk past the one-block bitmap buffer: an out-of-bounds read. */
    if (fs->blocks_per_group == 0 ||
        fs->blocks_per_group > fs->block_size * 8) {
        kprint("EXT2: Invalid blocks_per_group\n");
        kfree(fs, sizeof(ext2_fs_t));
        return NULL;
    }
    /* s_inodes_per_group is attacker-controlled on-disk metadata and is
     * used as a divisor in ext2_read_inode ((inode_num-1) / inodes_per_group)
     * and as a multiplier for inode-table sizing.  Zero would trap the
     * kernel with a divide error on the first inode read; an absurdly
     * large value yields nonsensical group math.  Reject both. */
    if (fs->inodes_per_group == 0 ||
        fs->inodes_per_group > fs->sb.s_inodes_count ||
        fs->inodes_per_group > fs->block_size * 8) {
        kprint("EXT2: Invalid inodes_per_group\n");
        kfree(fs, sizeof(ext2_fs_t));
        return NULL;
    }
    fs->group_count = (fs->sb.s_blocks_count + fs->blocks_per_group - 1) / fs->blocks_per_group;
    
    fs->inode_size = (fs->sb.s_rev_level >= 1) ? fs->sb.s_inode_size : EXT2_GOOD_OLD_INODE_SIZE;

    // Validate inode_size is non-zero and fits within a block
    /* ext2_read_inode() unconditionally memcpy's EXT2_GOOD_OLD_INODE_SIZE
     * bytes out of the block, and the csum helpers touch fixed offsets up to
     * 130, so anything smaller reads (and, with metadata_csum, WRITES) past
     * the block buffer.  A non-divisor also makes the last inode of a block
     * straddle its end. */
    if (fs->inode_size < EXT2_GOOD_OLD_INODE_SIZE ||
        (fs->inode_size & (fs->inode_size - 1)) != 0 ||
        (fs->block_size % fs->inode_size) != 0) {
        kprintf("ext2: bad inode size %u (block size %u)\n",
                fs->inode_size, fs->block_size);
        kfree(fs, sizeof(ext2_fs_t));
        return NULL;
    }
    if (fs->inode_size == 0 || fs->inode_size > fs->block_size) {
        kprint("EXT2: Invalid inode_size\n");
        kfree(fs, sizeof(ext2_fs_t));
        return NULL;
    }

    /* Sanity-cap the group descriptor table.  group_count is derived
     * from attacker-controllable s_blocks_count / s_blocks_per_group;
     * without a ceiling here a crafted superblock could ask for an
     * arbitrarily huge kmalloc.  16 MiB of BGDs covers >500k groups,
     * which is well past any realistic ext2/3/4 filesystem. */
    uint64_t raw_bgd_size64 = (uint64_t)fs->group_count * sizeof(ext2_group_desc_t);
    if (raw_bgd_size64 > (16ULL << 20)) {
        kprint("EXT2: group descriptor table too large; refusing mount\n");
        kfree(fs, sizeof(ext2_fs_t));
        return NULL;
    }
    uint32_t raw_bgd_size = (uint32_t)raw_bgd_size64;
    uint32_t bgd_blocks = (raw_bgd_size + fs->block_size - 1) / fs->block_size;
    uint32_t bgd_size = bgd_blocks * fs->block_size;

    fs->bgd = kmalloc(bgd_size);
    if (!fs->bgd) {
        kfree(fs, sizeof(ext2_fs_t));
        return NULL;
    }
    memset(fs->bgd, 0, bgd_size);

    /* Per-group dirty bitmap for deferred metadata flush (see ext2_fs_t).
     * Tiny (group_count/8 bytes); if it ever fails to allocate,
     * ext2_mark_meta_dirty() falls back to flushing immediately. */
    {
        uint32_t bgd_dirty_sz = (fs->group_count + 7u) / 8u;
        if (bgd_dirty_sz == 0) bgd_dirty_sz = 1;
        fs->bgd_dirty = kmalloc(bgd_dirty_sz);
        if (fs->bgd_dirty) memset(fs->bgd_dirty, 0, bgd_dirty_sz);
    }
    mutex_init(&fs->alloc_lock, "ext2_alloc");

    fs->last_alloc_group = 0;
    fs->last_alloc_bit = 0;

    // Read block group descriptor table (starts at block 2 for 1K blocks, block 1 for larger)
    uint32_t bgd_block = (fs->block_size == 1024) ? 2 : 1;

    /* Pick the on-disk descriptor size.  For ext2/3 and ext4-without-
     * 64BIT, it's 32 bytes (sizeof(ext2_group_desc_t)).  When the
     * filesystem advertises INCOMPAT_64BIT, the descriptor is 64
     * bytes — the second half carries the high words of every
     * block/inode address that the first half stores as a uint32_t
     * low half.  s_desc_size lives at superblock offset 0xFE (254).  */
    fs->desc_size = sizeof(ext2_group_desc_t);
    if (fs->sb.s_feature_incompat & EXT2F_INCOMPAT_64BIT) {
        uint16_t on_disk = *(uint16_t *)(sb_buf + 0xFE);
        if (on_disk == 0)               on_disk = 64;
        if (on_disk < 32 || on_disk > 1024 || (on_disk & 3)) {

            kprintf("ext2: implausible s_desc_size %u\n", on_disk);
            kfree(fs->bgd, bgd_size);
            kfree(fs, sizeof(ext2_fs_t));
            return NULL;
        }
        fs->desc_size = on_disk;
    }

    /* Re-size fs->bgd if the on-disk stride differs from 32 — we
     * still keep 32-byte slots in memory but the disk read stride
     * must match.  Read the raw descriptors into a temporary
     * staging buffer, copy the first 32 bytes of each, and reject
     * the mount if any high-half field is non-zero (we have no way
     * to address those blocks with uint32_t internals).  */
    uint32_t on_disk_total = fs->group_count * fs->desc_size;
    uint32_t on_disk_blocks = (on_disk_total + fs->block_size - 1) / fs->block_size;
    uint32_t on_disk_size   = on_disk_blocks * fs->block_size;
    uint8_t *gdt_raw = kmalloc(on_disk_size);
    if (!gdt_raw) {
        kfree(fs->bgd, bgd_size);
        kfree(fs, sizeof(ext2_fs_t));
        return NULL;
    }
    for (uint32_t i = 0; i < on_disk_blocks; i++) {
        ext2_read_block(fs, bgd_block + i, gdt_raw + i * fs->block_size);
    }
    for (uint32_t g = 0; g < fs->group_count; g++) {
        uint8_t *src = gdt_raw + g * fs->desc_size;
        /* Reject anything that would force >32-bit addressing.  */
        if (fs->desc_size >= 64) {
            uint32_t bb_hi = *(uint32_t *)(src + 32);   /* bg_block_bitmap_hi  */
            uint32_t ib_hi = *(uint32_t *)(src + 36);   /* bg_inode_bitmap_hi  */
            uint32_t it_hi = *(uint32_t *)(src + 40);   /* bg_inode_table_hi   */
            if (bb_hi || ib_hi || it_hi) {

                kprintf("ext2: bg %u uses >32-bit addresses (bb_hi=%x ib_hi=%x it_hi=%x) — refuse mount\n",
                        g, bb_hi, ib_hi, it_hi);
                kfree(gdt_raw, on_disk_size);
                kfree(fs->bgd, bgd_size);
                kfree(fs, sizeof(ext2_fs_t));
                return NULL;
            }
        }
        memcpy(&fs->bgd[g], src, sizeof(ext2_group_desc_t));
    }

    /* Per-group descriptor metadata_csum check.  The on-disk csum
     * field lives at byte 30 of the descriptor regardless of
     * desc_size.  Computation:
     *   crc32c(seed, le32(group)) -> crc32c(state, gd[0..29])
     *                             -> crc32c(state, 0_u16, 2)
     *                             -> crc32c(state, gd[32..desc_size-1])
     * For 32-byte descriptors the trailing range is empty.  Any
     * mismatch refuses the mount — same argument as superblock-csum.  */
    if (fs->sb.s_feature_ro_compat & EXT2F_ROCOMPAT_METADATA_CKSUM) {

        const uint16_t zero16 = 0;
        for (uint32_t g = 0; g < fs->group_count; g++) {
            uint8_t *gd = gdt_raw + g * fs->desc_size;
            uint32_t le_g = g;
            uint32_t c = crc32c_update(fs->csum_seed, &le_g, 4);
            c = crc32c_update(c, gd, 30);
            c = crc32c_update(c, &zero16, 2);
            if (fs->desc_size > 32)
                c = crc32c_update(c, gd + 32, fs->desc_size - 32);
            uint16_t expect = c & 0xFFFFu;
            uint16_t actual = *(uint16_t *)(gd + 30);
            if (expect != actual) {
                kprintf("ext2: bg descriptor %u csum mismatch want=%04x got=%04x — refuse mount\n",
                        g, expect, actual);
                kfree(gdt_raw, on_disk_size);
                kfree(fs->bgd, bgd_size);
                kfree(fs, sizeof(ext2_fs_t));
                return NULL;
            }
        }
        kprintf("ext2: %u group descriptor csums verified (desc_size=%u)\n",
                fs->group_count, fs->desc_size);
    }
    kfree(gdt_raw, on_disk_size);

    // Read root inode (inode 2)
    ext2_inode_t root_inode;
    if (ext2_read_inode(fs, EXT2_ROOT_INO, &root_inode) != 0) {
        kprint("EXT2: Failed to read root inode\n");
        kfree(fs->bgd, bgd_size);
        kfree(fs, sizeof(ext2_fs_t));
        return NULL;
    }

    // Initialize active block bitmap cache
    fs->active_bg_group = (uint32_t)-1;
    fs->active_bg_bitmap = kmalloc(fs->block_size);
    if (!fs->active_bg_bitmap) {
        kfree(fs->bgd, bgd_size);
        kfree(fs, sizeof(ext2_fs_t));
        return NULL;
    }
    memset(fs->active_bg_bitmap, 0, fs->block_size);

    // Initialize active inode bitmap cache
    fs->active_inode_bg_group = (uint32_t)-1;
    fs->active_inode_bg_bitmap = kmalloc(fs->block_size);
    if (!fs->active_inode_bg_bitmap) {
        kfree(fs->active_bg_bitmap, fs->block_size);
        kfree(fs->bgd, bgd_size);
        kfree(fs, sizeof(ext2_fs_t));
        return NULL;
    }
    memset(fs->active_inode_bg_bitmap, 0, fs->block_size);

    // Setup root node
    fs_node_t *root_node = ext2_alloc_node(fs, EXT2_ROOT_INO, &root_inode);
    if (!root_node) {
        kprint("EXT2: Failed to allocate root node\n");
        kfree(fs->active_inode_bg_bitmap, fs->block_size);
        kfree(fs->active_bg_bitmap, fs->block_size);
        kfree(fs->bgd, bgd_size);
        kfree(fs, sizeof(ext2_fs_t));
        return NULL;
    }

    strlcpy(root_node->name, "/", sizeof(root_node->name));
    root_node->name[sizeof(root_node->name) - 1] = '\0';
    
    ext2_node_t *root_ctx = (ext2_node_t *)(uintptr_t)root_node->impl;
    root_ctx->pin_count = 1; // Pin root node
    
    kprint("EXT2: Mounted successfully\n");
    return root_node;
}

/*
 * Read an ext2 volume label straight off the device, without mounting.
 * The superblock lives at a fixed byte offset of 1024 regardless of block
 * size; s_magic identifies the filesystem and s_volume_name[16] is the
 * label (space- or NUL-padded, possibly empty).
 */
int ext2_read_label(blkdev_t *dev, char *label, size_t len) {
    if (!dev || !label || len == 0) {
        return -1;
    }
    ext2_superblock_t sb;
    if (blkdev_read_bytes(dev, 1024, sizeof(sb), &sb) != sizeof(sb)) {
        return -1;
    }
    if (sb.s_magic != EXT2_SUPER_MAGIC) {
        return -1;  /* not an ext2/3/4 filesystem */
    }
    size_t i = 0;
    while (i < sizeof(sb.s_volume_name) && i + 1 < len &&
           sb.s_volume_name[i] != '\0') {
        label[i] = sb.s_volume_name[i];
        i++;
    }
    label[i] = '\0';
    return 0;
}

static filesystem_t ext2_filesystem = {
    .name = "ext2",
    .mount = ext2_mount,
    .read_label = ext2_read_label,
};

void ext2_init(void) {
    /* FS-01/FS-02: bring up the node-cache and inode-table serialization
     * before any mount can hand out or write back inodes. */
    mutex_init(&ext2_node_cache_lock, "ext2_ncache");
    mutex_init(&ext2_inode_table_lock, "ext2_itable");
    kprint("Initializing EXT2 Driver...\n");
    vfs_register_filesystem(&ext2_filesystem);
}

// Allocate a block from the filesystem
uint32_t ext2_alloc_block(ext2_fs_t *fs) {
    if (!fs) return 0;
    mutex_lock(&fs->alloc_lock);

    // Search each block group for a free block, starting from last allocation
    uint32_t start_group = fs->last_alloc_group;
    uint32_t group = start_group;

    do {
        if (fs->bgd[group].bg_free_blocks_count == 0) {
            group = (group + 1) % fs->group_count;
            continue;
        }
        
        // Read the block bitmap if it's not cached
        if (fs->active_bg_group != group) {
            fs->active_bg_group = (uint32_t)-1;
            if (ext2_read_block(fs, fs->bgd[group].bg_block_bitmap, fs->active_bg_bitmap) != fs->block_size) {
                group = (group + 1) % fs->group_count;
                continue;
            }
            fs->active_bg_group = group;
        }
        
        uint8_t *bitmap_buf = fs->active_bg_bitmap;
        uint32_t bits_in_group = fs->blocks_per_group;
        uint32_t start_bit = (group == fs->last_alloc_group) ? fs->last_alloc_bit : 0;
        uint32_t found_idx = 0;
        int found = 0;

        /*
         * EXT2-17: the returned block number was never checked against
         * s_blocks_count.  The last group's bitmap is padded out to
         * blocks_per_group bits, and those tail bits describe blocks that do
         * not exist -- mkfs sets them to 1, but nothing here required that.
         * A zero left in the padding was handed back as a normal allocation;
         * ext2_write_block() then silently declined to write it (returning 0,
         * which no caller checks) and the file ended up owning a block that
         * reads back as zeros with no error anywhere.
         *
         * Skip past any such bit and keep looking within the group.  The
         * in-memory bitmap is marked so the scan makes forward progress, but
         * nothing is written back: the mismatch is the filesystem's, and
         * repairing on-disk metadata from a corrupt image is fsck's job.
         */
        for (;;) {
            found = 0;
            // Pass 1: Search from start_bit to end
            if (ext2_find_next_zero_bit(bitmap_buf, bits_in_group, start_bit, bits_in_group, &found_idx)) {
                found = 1;
            }
            // Pass 2: Search from 0 to start_bit (wrap around within group)
            else if (start_bit > 0 && ext2_find_next_zero_bit(bitmap_buf, bits_in_group, 0, start_bit, &found_idx)) {
                found = 1;
            }
            if (!found) break;

            uint32_t cand = group * fs->blocks_per_group + found_idx +
                            fs->sb.s_first_data_block;
            if (cand < fs->sb.s_blocks_count) break;    /* a real block */

            bitmap_buf[found_idx / 8] |= (1 << (found_idx % 8));
            if (found_idx + 1 >= bits_in_group) { found = 0; break; }
            start_bit = found_idx + 1;
        }

        if (found) {
            uint32_t byte_idx = found_idx / 8;
            uint32_t bit_idx = found_idx % 8;
            
            // Mark as used
            bitmap_buf[byte_idx] |= (1 << bit_idx);

            // Write bitmap back
            ext2_write_block(fs, fs->bgd[group].bg_block_bitmap, bitmap_buf);

            // Update block group descriptor
            fs->bgd[group].bg_free_blocks_count--;

            // Calculate absolute block number
            uint32_t block_num = group * fs->blocks_per_group + found_idx + fs->sb.s_first_data_block;

            // Update superblock free blocks count
            fs->sb.s_free_blocks_count--;
            ext2_mark_meta_dirty(fs, group);

            // Update hints
            fs->last_alloc_group = group;
            fs->last_alloc_bit = (found_idx + 1) % bits_in_group;

            mutex_unlock(&fs->alloc_lock);
            return block_num;
        }

        group = (group + 1) % fs->group_count;
    } while (group != start_group);

    mutex_unlock(&fs->alloc_lock);
    return 0; // No free blocks
}

// Free a block
void ext2_free_block(ext2_fs_t *fs, uint32_t block_num) {
    if (!fs || block_num == 0) return;
    
    // Calculate which group this block belongs to
    uint32_t group = (block_num - fs->sb.s_first_data_block) / fs->blocks_per_group;
    uint32_t index = (block_num - fs->sb.s_first_data_block) % fs->blocks_per_group;
    
    if (group >= fs->group_count) return;
    mutex_lock(&fs->alloc_lock);

    // Read the block bitmap if it's not cached
    if (fs->active_bg_group != group) {
        fs->active_bg_group = (uint32_t)-1;
        if (ext2_read_block(fs, fs->bgd[group].bg_block_bitmap, fs->active_bg_bitmap) != fs->block_size) {
            mutex_unlock(&fs->alloc_lock);
            return;
        }
        fs->active_bg_group = group;
    }

    uint8_t *bitmap_buf = fs->active_bg_bitmap;

    uint32_t byte_idx = index / 8;
    uint32_t bit_idx = index % 8;
    
    /* Only account the free if the block was actually allocated; a double
     * free (bit already clear) must not inflate the free counts, which would
     * later let the allocator hand out the same block twice. */
    if (bitmap_buf[byte_idx] & (1 << bit_idx)) {
        bitmap_buf[byte_idx] &= ~(1 << bit_idx);
        ext2_write_block(fs, fs->bgd[group].bg_block_bitmap, bitmap_buf);

        fs->bgd[group].bg_free_blocks_count++;
        fs->sb.s_free_blocks_count++;
        ext2_mark_meta_dirty(fs, group);
    }
    mutex_unlock(&fs->alloc_lock);
}

// Allocate an inode
uint32_t ext2_alloc_inode(ext2_fs_t *fs, int is_dir) {
    if (!fs) return 0;
    mutex_lock(&fs->alloc_lock);

    // Search each block group for a free inode
    for (uint32_t group = 0; group < fs->group_count; group++) {
        if (fs->bgd[group].bg_free_inodes_count == 0) continue;
        
        // Read the inode bitmap if it's not cached
        if (fs->active_inode_bg_group != group) {
            fs->active_inode_bg_group = (uint32_t)-1;
            if (ext2_read_block(fs, fs->bgd[group].bg_inode_bitmap, fs->active_inode_bg_bitmap) != fs->block_size) {
                continue;
            }
            fs->active_inode_bg_group = group;
        }
        
        uint8_t *bitmap_buf = fs->active_inode_bg_bitmap;

        // Find the first free bit (skip reserved inodes in first group)
        uint32_t start = (group == 0) ? fs->sb.s_first_ino : 0;
        uint32_t bits_in_group = fs->inodes_per_group;
        
        for (uint32_t i = start; i < bits_in_group; i++) {
            // Optimization: Skip full 32-bit words
            if ((i % 32 == 0) && (i + 32 <= bits_in_group)) {
                if (*(uint32_t *)&bitmap_buf[i / 8] == 0xFFFFFFFF) {
                    i += 31;
                    continue;
                }
            }
            // Optimization: Skip full bytes
            else if ((i % 8 == 0) && (i + 8 <= bits_in_group)) {
                if (bitmap_buf[i / 8] == 0xFF) {
                    i += 7;
                    continue;
                }
            }

            uint32_t byte_idx = i / 8;
            uint32_t bit_idx = i % 8;
            
            if (!(bitmap_buf[byte_idx] & (1 << bit_idx))) {
                // Found free inode - allocate it
                bitmap_buf[byte_idx] |= (1 << bit_idx);
                
                // Write bitmap back
                ext2_write_block(fs, fs->bgd[group].bg_inode_bitmap, bitmap_buf);
                
                // Update block group descriptor
                fs->bgd[group].bg_free_inodes_count--;
                if (is_dir) {
                    fs->bgd[group].bg_used_dirs_count++;
                }
                
                // Calculate absolute inode number
                uint32_t inode_num = group * fs->inodes_per_group + i + 1;
                
                // Update superblock
                fs->sb.s_free_inodes_count--;
                ext2_mark_meta_dirty(fs, group);
                
                // Initialize the inode
                ext2_inode_t inode;
                memset(&inode, 0, sizeof(ext2_inode_t));
                uint32_t now = (uint32_t)get_time();
                inode.i_ctime = now;
                inode.i_mtime = now;
                inode.i_atime = now;
                
                ext2_write_inode(fs, inode_num, &inode);

                if (ext2_trace_on()) {
                    kprintf("ext2trace: IALLOC inode=%u is_dir=%d caller=%p\n",
                            inode_num, is_dir, __builtin_return_address(0));
                }
                mutex_unlock(&fs->alloc_lock);
                return inode_num;
            }
        }
    }

    mutex_unlock(&fs->alloc_lock);
    return 0; // No free inodes
}

// Free an inode
void ext2_free_inode(ext2_fs_t *fs, uint32_t inode_num, int was_dir) {
    if (ext2_trace_on()) {
        kprintf("ext2trace: IFREE  inode=%u was_dir=%d caller=%p\n",
                inode_num, was_dir, __builtin_return_address(0));
    }
    if (!fs || inode_num == 0) return;

    /* Evict any cached fs_node_t / ext2_node_t for this inode.  The
     * inode is about to be reusable for a fresh mknod (potentially
     * with a different file type); if we leave the stale cache slot
     * in place, the next finddir/lookup that reaches the cache lookup
     * loop will hand back the OLD node — same inode_num but with the
     * old mode bits and the old node->flags (FS_FILE vs FS_PIPE vs
     * FS_CHARDEVICE).  That manifested as torture_ipc tests 7 and 9
     * failing S_ISFIFO/S_ISCHR after mknod-of-a-recycled-inode.
     *
     * FS-01: take ext2_node_cache_lock across the whole scan.  Slot
     * selection + kfree() of the scratch buffers + zeroing the slot must
     * be atomic with respect to ext2_alloc_node(), which holds the same
     * lock while it selects and populates a slot; otherwise the two race
     * onto the same slot (double-free / half-populated node). */
    mutex_lock(&ext2_node_cache_lock);
    for (int i = 0; i < EXT2_NODE_CACHE_SIZE; i++) {
        if (ext2_node_cache[i].fs == fs &&
            ext2_node_cache[i].inode_num == inode_num &&
            ext2_node_cache[i].pin_count == 0) {
            uint32_t bs = fs->block_size;
            if (ext2_node_cache[i].block_buf) {
                kfree(ext2_node_cache[i].block_buf, bs);
                ext2_node_cache[i].block_buf = NULL;
            }
            if (ext2_node_cache[i].indirect_buf) {
                kfree(ext2_node_cache[i].indirect_buf, bs);
                ext2_node_cache[i].indirect_buf = NULL;
            }
            if (ext2_node_cache[i].dindirect_buf) {
                kfree(ext2_node_cache[i].dindirect_buf, bs);
                ext2_node_cache[i].dindirect_buf = NULL;
            }
            if (ext2_node_cache[i].tindirect_buf) {
                kfree(ext2_node_cache[i].tindirect_buf, bs);
                ext2_node_cache[i].tindirect_buf = NULL;
            }
            ext2_node_cache[i].fs = NULL;
            ext2_node_cache[i].inode_num = 0;
            /* fs_node_t side: zero it so a stale finddir-by-pointer
             * comparison can't see the old flags either. */
            memset(&ext2_fs_node_cache[i], 0, sizeof(ext2_fs_node_cache[i]));
        }
    }
    mutex_unlock(&ext2_node_cache_lock);

    // Calculate which group this inode belongs to
    uint32_t group = (inode_num - 1) / fs->inodes_per_group;
    uint32_t index = (inode_num - 1) % fs->inodes_per_group;

    if (group >= fs->group_count) return;
    mutex_lock(&fs->alloc_lock);

    // Read the inode bitmap if it's not cached
    if (fs->active_inode_bg_group != group) {
        fs->active_inode_bg_group = (uint32_t)-1;
        if (ext2_read_block(fs, fs->bgd[group].bg_inode_bitmap, fs->active_inode_bg_bitmap) != fs->block_size) {
            mutex_unlock(&fs->alloc_lock);
            return;
        }
        fs->active_inode_bg_group = group;
    }

    uint8_t *bitmap_buf = fs->active_inode_bg_bitmap;

    uint32_t byte_idx = index / 8;
    uint32_t bit_idx = index % 8;
    
    /* Only account the free if the inode was actually allocated.  A double
     * free (bit already clear — e.g. an error-path rollback that also runs the
     * normal free) must not inflate the free counts, which would later let the
     * allocator over-hand inodes. */
    if (bitmap_buf[byte_idx] & (1 << bit_idx)) {
        bitmap_buf[byte_idx] &= ~(1 << bit_idx);
        ext2_write_block(fs, fs->bgd[group].bg_inode_bitmap, bitmap_buf);

        fs->bgd[group].bg_free_inodes_count++;
        if (was_dir) {
            fs->bgd[group].bg_used_dirs_count--;
        }
        fs->sb.s_free_inodes_count++;
        ext2_mark_meta_dirty(fs, group);
    }
    mutex_unlock(&fs->alloc_lock);
}

static uint8_t ext2_dirent_type_from_mode(uint16_t mode) {
    switch (mode & S_IFMT) {
        case S_IFREG: return EXT2_FT_REG_FILE;
        case S_IFDIR: return EXT2_FT_DIR;
        case S_IFCHR: return EXT2_FT_CHRDEV;
        case S_IFBLK: return EXT2_FT_BLKDEV;
        case S_IFIFO: return EXT2_FT_FIFO;
        case S_IFLNK: return EXT2_FT_SYMLINK;
        default: return EXT2_FT_UNKNOWN;
    }
}

static uint8_t ext2_file_type_to_dt(uint8_t ext2_type) {
    switch (ext2_type) {
        case EXT2_FT_REG_FILE: return DT_REG;
        case EXT2_FT_DIR:      return DT_DIR;
        case EXT2_FT_CHRDEV:   return DT_CHR;
        case EXT2_FT_BLKDEV:   return DT_BLK;
        case EXT2_FT_FIFO:     return DT_FIFO;
        case EXT2_FT_SOCK:     return DT_SOCK;
        case EXT2_FT_SYMLINK:  return DT_LNK;
        default:               return DT_UNKNOWN;
    }
}

static int ext2_free_indirect_tree(ext2_fs_t *fs, uint32_t block_num, uint32_t depth) {
    uint32_t entries_per_block;
    uint32_t *block_buf;

    if (!fs || block_num == 0) return 0;
    if (depth == 0) {
        ext2_free_block(fs, block_num);
        return 0;
    }

    entries_per_block = fs->block_size / sizeof(uint32_t);
    block_buf = kmalloc(fs->block_size);
    if (!block_buf) return -ENOMEM;

    if (ext2_read_block(fs, block_num, block_buf) != fs->block_size) {
        kfree(block_buf, fs->block_size);
        return -EIO;
    }

    for (uint32_t i = 0; i < entries_per_block; i++) {
        if (block_buf[i] != 0) {
            int ret = ext2_free_indirect_tree(fs, block_buf[i], depth - 1);
            if (ret != 0) {
                kfree(block_buf, fs->block_size);
                return ret;
            }
        }
    }

    kfree(block_buf, fs->block_size);
    ext2_free_block(fs, block_num);
    return 0;
}

static int ext2_free_inode_blocks(ext2_fs_t *fs, ext2_inode_t *inode) {
    if (!fs || !inode) return -EINVAL;

    /*
     * For an ext4 extent inode, i_block[] is NOT an array of block pointers:
     * it holds the packed ext4_extent_header plus up to four extents.  Walking
     * it as direct/indirect pointers frees blocks that belong to other files
     * (i_block[0] is magic|entry-count, i_block[1] is max|depth, and with four
     * extents i_block[12..14] -- the "indirect" slots -- are extent payload,
     * every non-zero word of which would then be read as an indirect block and
     * its contents freed too).  One unlink corrupts the whole filesystem.
     *
     * Extent-tree teardown is not implemented, so refuse rather than destroy:
     * the blocks leak, which fsck can reclaim, instead of being handed to the
     * next allocator while another file still points at them.
     */
    if (inode->i_flags & EXT4_EXTENTS_FL) {
        return -EOPNOTSUPP;
    }

    for (uint32_t i = 0; i < 12; i++) {
        if (inode->i_block[i] != 0) {
            ext2_free_block(fs, inode->i_block[i]);
            inode->i_block[i] = 0;
        }
    }
    if (inode->i_block[12] != 0) {
        int ret = ext2_free_indirect_tree(fs, inode->i_block[12], 1);
        if (ret != 0) return ret;
        inode->i_block[12] = 0;
    }
    if (inode->i_block[13] != 0) {
        int ret = ext2_free_indirect_tree(fs, inode->i_block[13], 2);
        if (ret != 0) return ret;
        inode->i_block[13] = 0;
    }
    if (inode->i_block[14] != 0) {
        int ret = ext2_free_indirect_tree(fs, inode->i_block[14], 3);
        if (ret != 0) return ret;
        inode->i_block[14] = 0;
    }

    inode->i_blocks = 0;
    inode->i_size = 0;
    return 0;
}

int ext2_truncate(fs_node_t *node, off_t length) {
    ext2_node_t *ctx;
    ext2_fs_t *fs;

    if (!node) return -EINVAL;
    if (length < 0) return -EINVAL;
    ctx = (ext2_node_t *)(uintptr_t)node->impl;
    if (!ctx) return -EINVAL;
    fs = ctx->fs;
    if (EXT2_RO_REFUSE(fs)) return -EROFS;

    /* i_size is tracked as a 32-bit field throughout this driver, so refuse
     * a truncation that could not be represented rather than silently wrap. */
    if ((uint64_t)length > 0xFFFFFFFFULL) return -EFBIG;

    off_t old_size = (off_t)ctx->inode.i_size;
    if (length == old_size) {
        return 0;
    }

    mutex_lock(&ctx->lock);
    int ret = 0;

    if (length == 0) {
        /* Release every data block; ext2_free_inode_blocks also clears
         * i_size and i_blocks. */
        ret = ext2_free_inode_blocks(fs, &ctx->inode);
    } else if (length > old_size) {
        /*
         * Grow.  ext2 stores files sparsely: the region between the old and
         * the new end-of-file is a hole that reads back as zeros
         * (ext2_get_block_num returns 0 for an unmapped logical block and
         * ext2_read_block zero-fills it).  So growing is just a matter of
         * publishing the larger size -- data blocks are allocated lazily by
         * the write path on first store.  This is exactly what ftruncate(2)
         * on a freshly created (empty) file does, the precondition for most
         * of the mmap(2) conformance tests.
         */
        ctx->inode.i_size = (uint32_t)length;
    } else {
        /*
         * Shrink to a smaller, non-zero length.  Correctly releasing only the
         * data blocks wholly past the new end-of-file (and collapsing
         * now-empty indirect blocks) is not implemented; refuse rather than
         * risk leaking or double-freeing blocks.  Shrink-to-0 takes the fast
         * path above.
         */
        ret = -EOPNOTSUPP;
    }

    if (ret == 0) {
        uint32_t now = (uint32_t)get_time();
        ctx->inode.i_mtime = now;
        ctx->inode.i_ctime = now;
        node->length = (uint32_t)length;
        ret = ext2_write_inode(fs, ctx->inode_num, &ctx->inode);
        if (ret != 0) {
            ret = -EIO;
        }
    }
    mutex_unlock(&ctx->lock);
    return ret;
}

// Add directory entry
static int ext2_add_entry(fs_node_t *dir, const char *name, uint32_t inode, uint8_t file_type) {
    if (!dir || !name || inode == 0) return -1;
    
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)dir->impl;
    ext2_fs_t *fs = ctx->fs;
    
    uint32_t name_len = strlen(name);
    if (name_len > 255) return -1;
    
    // Calculate required entry size (aligned to 4 bytes)
    uint32_t required_size = ((8 + name_len + 3) / 4) * 4;
    
    mutex_lock(&ctx->lock);

    // Invalidate dcache entry if it matches
    for (int k = 0; k < EXT2_DCACHE_SIZE; k++) {
        if (ctx->dcache[k].inode_num != 0 &&
            ctx->dcache[k].name_len == name_len &&
            memcmp(ctx->dcache[k].name, name, name_len) == 0) {
            ctx->dcache[k].inode_num = 0;
            // No break, clear all possible duplicates just in case
        }
    }

    // Lazy allocate
    uint32_t block_size = fs->block_size;
    if (!ctx->block_buf) ctx->block_buf = kmalloc(block_size);
    if (!ctx->indirect_buf) ctx->indirect_buf = kmalloc(block_size);
    if (!ctx->dindirect_buf) ctx->dindirect_buf = kmalloc(block_size);
    if (!ctx->tindirect_buf) ctx->tindirect_buf = kmalloc(block_size);

    if (!ctx->block_buf || !ctx->indirect_buf || !ctx->dindirect_buf || !ctx->tindirect_buf) {
        mutex_unlock(&ctx->lock);
        return -1;
    }

    uint8_t *block_buf = ctx->block_buf;
    uint32_t *indirect = ctx->indirect_buf;
    uint32_t *dindirect = ctx->dindirect_buf;
    uint32_t *tindirect = ctx->tindirect_buf;

    uint32_t dir_size = ctx->inode.i_size;
    uint32_t pos = 0;
    int result = -1;
    
    // Search for space in existing blocks
    while (pos < dir_size) {
        uint32_t block_idx = pos / fs->block_size;
        uint32_t block_off = pos % fs->block_size;
        uint32_t block_num = ext2_get_block_num(fs, &ctx->inode, block_idx, indirect, dindirect, tindirect);
        
        if (block_num == 0) break;
        ext2_read_block(fs, block_num, block_buf);
        
        while (block_off + 8 <= fs->block_size && pos < dir_size) {
            ext2_dirent_t *de = (ext2_dirent_t *)(block_buf + block_off);
            
            if (de->rec_len < 8 || block_off + de->rec_len > fs->block_size) break;

            // Calculate actual size needed by this entry
            uint32_t actual_size = ((8 + de->name_len + 3) / 4) * 4;
            /* A corrupt on-disk name_len can make actual_size exceed this
             * record's rec_len; the unsigned `slack` would then underflow to a
             * huge value, pass the fit test, and place new_de past the block
             * buffer (heap overflow).  Treat it as a malformed entry. */
            if (actual_size > de->rec_len) break;
            uint32_t slack = de->rec_len - actual_size;
            
            // Can we fit the new entry in the slack space?
            if (slack >= required_size && de->inode != 0) {
                // Split this entry
                de->rec_len = actual_size;

                ext2_dirent_t *new_de = (ext2_dirent_t *)(block_buf + block_off + actual_size);
                new_de->inode = inode;
                new_de->rec_len = slack;
                new_de->name_len = name_len;
                new_de->file_type = file_type;
                memcpy(new_de->name, name, name_len);

                ext2_write_block(fs, block_num, block_buf);
                if (ext2_trace_on() && ctx->inode_num <= EXT2_TRACE_PARENT_LIMIT) {
                    kprintf("ext2trace: ADD    parent=%u name='%s' child=%u ft=%u (split)\n",
                            ctx->inode_num, name, inode, file_type);
                }
                result = 0;
                goto done;
            }

            // Can we reuse a deleted entry?
            if (de->inode == 0 && de->rec_len >= required_size) {
                de->inode = inode;
                de->name_len = name_len;
                de->file_type = file_type;
                memcpy(de->name, name, name_len);

                ext2_write_block(fs, block_num, block_buf);
                if (ext2_trace_on() && ctx->inode_num <= EXT2_TRACE_PARENT_LIMIT) {
                    kprintf("ext2trace: ADD    parent=%u name='%s' child=%u ft=%u (reuse)\n",
                            ctx->inode_num, name, inode, file_type);
                }
                result = 0;
                goto done;
            }
            
            block_off += de->rec_len;
            pos += de->rec_len;
        }

        /*
         * Guarantee forward progress.  The inner scan can break early on a
         * malformed entry (rec_len < 8, a record that runs past the block, or
         * name_len larger than the record) without having advanced pos into the
         * next block.  If pos is left mid-block the outer loop recomputes the
         * same block_idx, re-reads the same block, breaks again, and spins
         * forever in the kernel -- an unkillable 100%-CPU hang (observed adding
         * a directory entry into a directory with a corrupt/zero block).  Skip
         * to the start of the next block so a bad block can never trap us.
         */
        uint32_t block_end = (block_idx + 1) * fs->block_size;
        if (pos < block_end)
            pos = block_end;
    }

    // No space found - need to allocate a new block
    uint32_t new_block_idx = dir_size / fs->block_size;

    // Use ext2_alloc_inode_block to allocate and attach the block
    if (ext2_alloc_inode_block(fs, &ctx->inode, new_block_idx, indirect, dindirect, tindirect) != 0) {
        result = -1; // Out of space
        goto cleanup;
    }

    // Get the block number of the newly allocated block
    uint32_t new_block = ext2_get_block_num(fs, &ctx->inode, new_block_idx, indirect, dindirect, tindirect);
    if (new_block == 0) {
        result = -1;
        goto cleanup;
    }

    // Zero the new block
    memset(block_buf, 0, fs->block_size);

    // Create the new entry
    ext2_dirent_t *de = (ext2_dirent_t *)block_buf;
    de->inode = inode;
    de->rec_len = fs->block_size; // Entry spans entire block
    de->name_len = name_len;
    de->file_type = file_type;
    memcpy(de->name, name, name_len);

    ext2_write_block(fs, new_block, block_buf);
    if (ext2_trace_on() && ctx->inode_num <= EXT2_TRACE_PARENT_LIMIT) {
        kprintf("ext2trace: ADD    parent=%u name='%s' child=%u ft=%u (new block)\n",
                ctx->inode_num, name, inode, file_type);
    }
    
    // Update directory size and timestamps
    ctx->inode.i_size += fs->block_size;
    ctx->inode.i_blocks += (fs->block_size / 512);
    
    uint32_t now = (uint32_t)get_time();
    ctx->inode.i_mtime = now;
    ctx->inode.i_ctime = now;
    
    ext2_write_inode(fs, ctx->inode_num, &ctx->inode);
    result = 0;
    
    // Invalidate readdir cache
    ctx->last_readdir_idx = (uint64_t)-1;
    ctx->last_readdir_pos = 0;
    goto cleanup;

done:
    ext2_write_inode(fs, ctx->inode_num, &ctx->inode);

cleanup:
    mutex_unlock(&ctx->lock);
    return result;
}

// Implement ext2_link
int ext2_link(fs_node_t *parent, fs_node_t *source, const char *name) {
    if (!parent || !source || !name || !name[0]) return -EINVAL;
    if ((parent->flags & 0x7) != FS_DIRECTORY) return -ENOTDIR;
    if ((source->flags & 0x7) == FS_DIRECTORY) return -EPERM; // POSIX doesn't allow hard links to directories
    {
        ext2_node_t *pc = (ext2_node_t *)(uintptr_t)parent->impl;
        if (pc && EXT2_RO_REFUSE(pc->fs)) return -EROFS;
    }

    ext2_node_t *dir_ctx = (ext2_node_t *)(uintptr_t)parent->impl;
    ext2_node_t *source_ctx = (ext2_node_t *)(uintptr_t)source->impl;
    ext2_fs_t *fs = dir_ctx->fs;

    if (ext2_finddir(parent, (char *)name) != NULL) return -EEXIST;

    // Increment links_count
    source_ctx->inode.i_links_count++;
    source_ctx->inode.i_ctime = (uint32_t)get_time();
    if (ext2_write_inode(fs, source_ctx->inode_num, &source_ctx->inode) != 0) {
        source_ctx->inode.i_links_count--;
        return -EIO;
    }

    // Add entry to directory
    uint8_t file_type = EXT2_FT_REG_FILE;
    uint32_t s_flags = source->flags & 0x7;
    if (s_flags == FS_CHARDEVICE) file_type = EXT2_FT_CHRDEV;
    else if (s_flags == FS_BLOCKDEVICE) file_type = EXT2_FT_BLKDEV;
    else if (s_flags == FS_PIPE) file_type = EXT2_FT_FIFO;
    else if (s_flags == FS_SYMLINK) file_type = EXT2_FT_SYMLINK;

    if (ext2_add_entry(parent, name, source_ctx->inode_num, file_type) != 0) {
        source_ctx->inode.i_links_count--;
        ext2_write_inode(fs, source_ctx->inode_num, &source_ctx->inode);
        return -EIO;
    }

    return 0;
}

// Implement ext2_rename
int ext2_rename(fs_node_t *old_parent, const char *old_name, fs_node_t *new_parent, const char *new_name) {
    if (!old_parent || !old_name || !new_parent || !new_name) return -EINVAL;
    {
        ext2_node_t *oc = (ext2_node_t *)(uintptr_t)old_parent->impl;
        if (oc && EXT2_RO_REFUSE(oc->fs)) return -EROFS;
    }

    fs_node_t *old_node = ext2_finddir(old_parent, (char *)old_name);
    if (!old_node) return -ENOENT;

    /* FS-05: old_parent, new_parent and old_node are all bare node-cache
     * slots that we keep dereferencing across the finddir/alloc_node/
     * unlink/rmdir calls below — every one of which can recycle an
     * unpinned slot.  Pin them for the whole operation so none is reused
     * out from under us, and release them at the single `out:` exit.
     * (new_node is pinned separately once found.) */
    ext2_node_open(old_parent);
    ext2_node_open(new_parent);
    ext2_node_open(old_node);

    ext2_node_t *old_node_ctx = (ext2_node_t *)(uintptr_t)old_node->impl;
    fs_node_t *new_node = NULL;
    int new_node_pinned = 0;
    int rc = -EIO;

    /* FS-07: reject moving a directory into itself or into one of its own
     * descendants — that splices a detached cycle out of the tree (POSIX
     * EINVAL).  Only a directory can create a loop.  Walk new_parent's
     * ancestry via ".."; each ext2_finddir() pins its own parent for the
     * duration of that lookup (FS-05), so this walk is safe. */
    if ((old_node->flags & 0x7) == FS_DIRECTORY) {
        fs_node_t *anc = new_parent;
        int guard = 0;
        while (anc && guard++ < EXT2_NODE_CACHE_SIZE) {
            ext2_node_t *ac = (ext2_node_t *)(uintptr_t)anc->impl;
            if (!ac) break;
            if (ac->inode_num == old_node_ctx->inode_num) { rc = -EINVAL; goto out; }
            if (ac->inode_num == EXT2_ROOT_INO) break;
            fs_node_t *parent = ext2_finddir(anc, "..");
            if (!parent || parent == anc) break;
            anc = parent;
        }
    }

    // Check if new path exists and handle replacement
    new_node = ext2_finddir(new_parent, (char *)new_name);
    if (new_node) {
        ext2_node_open(new_node);
        new_node_pinned = 1;
        if ((old_node->flags & 0x7) == FS_DIRECTORY) {
            if ((new_node->flags & 0x7) != FS_DIRECTORY) { rc = -ENOTDIR; goto out; }
            if (!ext2_dir_is_empty(new_node)) { rc = -ENOTEMPTY; goto out; }
            ext2_rmdir(new_parent, new_name);
        } else {
            if ((new_node->flags & 0x7) == FS_DIRECTORY) { rc = -EISDIR; goto out; }
            ext2_unlink(new_parent, new_name);
        }
    }

    uint8_t file_type = EXT2_FT_REG_FILE;
    uint32_t flags = old_node->flags & 0x7;
    if (flags == FS_DIRECTORY) file_type = EXT2_FT_DIR;
    else if (flags == FS_SYMLINK) file_type = EXT2_FT_SYMLINK;
    else if (flags == FS_CHARDEVICE) file_type = EXT2_FT_CHRDEV;
    else if (flags == FS_BLOCKDEVICE) file_type = EXT2_FT_BLKDEV;
    else if (flags == FS_PIPE) file_type = EXT2_FT_FIFO;

    // Add new entry
    if (ext2_add_entry(new_parent, new_name, old_node_ctx->inode_num, file_type) != 0) {
        rc = -EIO;
        goto out;
    }

    // Remove old entry
    if (ext2_remove_entry(old_parent, old_name) != 0) {
        ext2_remove_entry(new_parent, new_name); // Try to roll back
        rc = -EIO;
        goto out;
    }

    // Update parent link counts if it's a directory moving to a different parent
    if ((old_node->flags & 0x7) == FS_DIRECTORY && old_parent != new_parent) {
        ext2_node_t *old_p_ctx = (ext2_node_t *)(uintptr_t)old_parent->impl;
        ext2_node_t *new_p_ctx = (ext2_node_t *)(uintptr_t)new_parent->impl;
        ext2_fs_t *fs = old_node_ctx->fs;

        old_p_ctx->inode.i_links_count--;
        ext2_write_inode(fs, old_p_ctx->inode_num, &old_p_ctx->inode);

        new_p_ctx->inode.i_links_count++;
        ext2_write_inode(fs, new_p_ctx->inode_num, &new_p_ctx->inode);

        // Update ".." in the moved directory
        if (!old_node_ctx->indirect_buf) old_node_ctx->indirect_buf = kmalloc(fs->block_size);
        if (!old_node_ctx->dindirect_buf) old_node_ctx->dindirect_buf = kmalloc(fs->block_size);
        if (!old_node_ctx->tindirect_buf) old_node_ctx->tindirect_buf = kmalloc(fs->block_size);
        if (!old_node_ctx->block_buf) old_node_ctx->block_buf = kmalloc(fs->block_size);

        if (old_node_ctx->block_buf) {
            uint32_t dotdot_block = ext2_get_block_num(fs, &old_node_ctx->inode, 0,
                                                       old_node_ctx->indirect_buf,
                                                       old_node_ctx->dindirect_buf,
                                                       old_node_ctx->tindirect_buf);
            if (dotdot_block != 0) {
                ext2_read_block(fs, dotdot_block, old_node_ctx->block_buf);
                ext2_dirent_t *dot = (ext2_dirent_t *)old_node_ctx->block_buf;
                /* A63: dot->rec_len is an untrusted uint16 read from disk.
                 * Bound it so the '..' entry (8-byte dirent header + its
                 * 2-char name) lies wholly inside the block buffer before
                 * we dereference or write through dotdot.  A corrupt '.'
                 * rec_len (e.g. 0xFFFA) would otherwise point far past
                 * block_buf and read/write out of bounds. */
                uint32_t dot_reclen = dot->rec_len;
                if (dot_reclen >= sizeof(ext2_dirent_t) &&
                    dot_reclen + sizeof(ext2_dirent_t) + 2 <= fs->block_size) {
                    ext2_dirent_t *dotdot = (ext2_dirent_t *)(old_node_ctx->block_buf + dot_reclen);
                    if (dotdot->name_len == 2 && dotdot->name[0] == '.' && dotdot->name[1] == '.') {
                        dotdot->inode = new_p_ctx->inode_num;
                        ext2_write_block(fs, dotdot_block, old_node_ctx->block_buf);
                    }
                }
            }
        }
    }

    rc = 0;

out:
    /* Drop the pins in reverse order.  A new_node whose entry we replaced
     * via ext2_unlink() (which, seeing our pin, deferred the inode free)
     * gets its deferred delete completed here by ext2_node_close(). */
    if (new_node_pinned) ext2_node_close(new_node);
    ext2_node_close(old_node);
    ext2_node_close(new_parent);
    ext2_node_close(old_parent);
    return rc;
}

// Remove directory entry
static int ext2_remove_entry(fs_node_t *dir, const char *name) {
    if (!dir || !name) return -1;
    
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)dir->impl;
    ext2_fs_t *fs = ctx->fs;
    // Optimization: Calculate strlen once
    size_t name_len = strlen(name);

    mutex_lock(&ctx->lock);

    // Invalidate dcache entry if it matches
    for (int k = 0; k < EXT2_DCACHE_SIZE; k++) {
        if (ctx->dcache[k].inode_num != 0 &&
            ctx->dcache[k].name_len == name_len &&
            memcmp(ctx->dcache[k].name, name, name_len) == 0) {
            ctx->dcache[k].inode_num = 0;
            // No break, clear all possible duplicates just in case
        }
    }

    // Lazy allocate
    uint32_t block_size = fs->block_size;
    if (!ctx->block_buf) ctx->block_buf = kmalloc(block_size);
    if (!ctx->indirect_buf) ctx->indirect_buf = kmalloc(block_size);
    if (!ctx->dindirect_buf) ctx->dindirect_buf = kmalloc(block_size);
    if (!ctx->tindirect_buf) ctx->tindirect_buf = kmalloc(block_size);

    if (!ctx->block_buf || !ctx->indirect_buf || !ctx->dindirect_buf || !ctx->tindirect_buf) {
        mutex_unlock(&ctx->lock);
        return -1;
    }

    uint8_t *block_buf = ctx->block_buf;
    uint32_t *indirect = ctx->indirect_buf;
    uint32_t *dindirect = ctx->dindirect_buf;
    uint32_t *tindirect = ctx->tindirect_buf;

    uint32_t dir_size = ctx->inode.i_size;
    uint32_t pos = 0;
    int result = -1;
    
    while (pos < dir_size) {
        uint32_t block_idx = pos / fs->block_size;
        uint32_t block_off = pos % fs->block_size;
        uint32_t block_num = ext2_get_block_num(fs, &ctx->inode, block_idx, indirect, dindirect, tindirect);
        
        if (block_num == 0) break;
        ext2_read_block(fs, block_num, block_buf);
        
        ext2_dirent_t *prev_de = NULL;
        
        while (block_off + 8 <= fs->block_size && pos < dir_size) {
            ext2_dirent_t *de = (ext2_dirent_t *)(block_buf + block_off);
            
            if (de->rec_len < 8 || block_off + de->rec_len > fs->block_size) break;
            
            // Is this the entry to remove?
            if (de->inode != 0 && de->rec_len >= 8 &&
            de->name_len <= (uint32_t)(de->rec_len - 8) &&
            de->name_len == name_len &&
                memcmp(de->name, name, de->name_len) == 0) {

                uint32_t removed_inode = de->inode;

                // Merge with previous entry if possible
                if (prev_de) {
                    prev_de->rec_len += de->rec_len;
                } else {
                    // First entry in block - just mark as deleted
                    de->inode = 0;
                }

                ext2_write_block(fs, block_num, block_buf);

                if (ext2_trace_on() && ctx->inode_num <= EXT2_TRACE_PARENT_LIMIT) {
                    kprintf("ext2trace: REMOVE parent=%u name='%s' child=%u\n",
                            ctx->inode_num, name, removed_inode);
                }
                
                // Update directory mtime and ctime
                uint32_t now = (uint32_t)get_time();
                ctx->inode.i_mtime = now;
                ctx->inode.i_ctime = now;
                ext2_write_inode(fs, ctx->inode_num, &ctx->inode);
                
                result = 0;

                // Invalidate readdir cache
                ctx->last_readdir_idx = (uint64_t)-1;
                ctx->last_readdir_pos = 0;

                goto cleanup;
            }
            
            prev_de = de;
            block_off += de->rec_len;
            pos += de->rec_len;
        }

        /* Same guard ext2_add_entry() carries: the inner loop `break`s on a
         * malformed record (rec_len < 8, or one running past the block) WITHOUT
         * advancing pos.  The outer loop then recomputes the identical
         * block_idx, re-reads the same block, breaks again -- an unkillable
         * 100%-CPU kernel spin, here with ctx->lock held so every other user of
         * this directory blocks behind it.  Skip to the next block instead. */
        uint32_t block_end = (block_idx + 1) * fs->block_size;
        if (pos < block_end)
            pos = block_end;
    }

cleanup:
    mutex_unlock(&ctx->lock);
    return result;
}

static int ext2_mknod(fs_node_t *dir, const char *name, uint16_t mode, uint32_t dev) {
    ext2_node_t *dir_ctx;
    ext2_fs_t *fs;
    ext2_inode_t inode;
    uint32_t inode_num;
    uint16_t type;
    int is_dir = 0;

    if (!dir || !name || !name[0]) return -EINVAL;
    if ((dir->flags & 0x7) != FS_DIRECTORY) return -ENOTDIR;
    {
        /* Every other mutating op checks this; mknod and symlink did not, so
         * they allocated inodes and blocks and rewrote the superblock counts
         * on a volume mounted -o ro, or force-mounted read-only because it
         * carries RO_COMPAT features this driver does not implement. */
        ext2_node_t *roc = (ext2_node_t *)(uintptr_t)dir->impl;
        if (roc && EXT2_RO_REFUSE(roc->fs)) return -EROFS;
    }
    if (!strcmp(name, ".") || !strcmp(name, "..")) return -EINVAL;
    if (ext2_finddir(dir, (char *)name) != NULL) return -EEXIST;

    type = mode & S_IFMT;
    if (type == 0) {
        type = S_IFREG;
        mode |= S_IFREG;
    }
    if (type == S_IFDIR) return -EISDIR;

    dir_ctx = (ext2_node_t *)(uintptr_t)dir->impl;
    fs = dir_ctx->fs;
    inode_num = ext2_alloc_inode(fs, is_dir);
    if (inode_num == 0) return -ENOSPC;

    memset(&inode, 0, sizeof(inode));
    inode.i_mode = mode;
    inode.i_uid = dir->uid;
    inode.i_gid = dir->gid;
    inode.i_links_count = 1;
    if (type == S_IFCHR || type == S_IFBLK) {
        inode.i_block[0] = dev;
    }
    uint32_t now = (uint32_t)get_time();
    inode.i_atime = now;
    inode.i_mtime = now;
    inode.i_ctime = now;

    if (ext2_write_inode(fs, inode_num, &inode) != 0) {
        ext2_free_inode(fs, inode_num, is_dir);
        return -EIO;
    }

    if (ext2_add_entry(dir, name, inode_num, ext2_dirent_type_from_mode(mode)) != 0) {
        ext2_free_inode(fs, inode_num, is_dir);
        return -EIO;
    }

    return 0;
}

static int ext2_symlink(fs_node_t *dir, const char *target, const char *name) {
    if (!dir || !target || !name || !name[0]) return -EINVAL;
    if ((dir->flags & 0x7) != FS_DIRECTORY) return -ENOTDIR;
    {
        /* See ext2_mknod(): read-only mounts must refuse this too. */
        ext2_node_t *roc = (ext2_node_t *)(uintptr_t)dir->impl;
        if (roc && EXT2_RO_REFUSE(roc->fs)) return -EROFS;
    }
    if (!strcmp(name, ".") || !strcmp(name, "..")) return -EINVAL;
    if (ext2_finddir(dir, (char *)name) != NULL) return -EEXIST;

    ext2_node_t *dir_ctx = (ext2_node_t *)(uintptr_t)dir->impl;
    ext2_fs_t *fs = dir_ctx->fs;
    uint32_t target_len = strlen(target);

    uint32_t inode_num = ext2_alloc_inode(fs, 0);
    if (inode_num == 0) return -ENOSPC;

    ext2_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.i_mode = EXT2_S_IFLNK | 0777;
    inode.i_uid = 0;
    inode.i_gid = 0;
    inode.i_links_count = 1;
    inode.i_size = target_len;
    uint32_t now = (uint32_t)get_time();
    inode.i_atime = now;
    inode.i_mtime = now;
    inode.i_ctime = now;

    if (target_len <= 60) {
        /* Fast symlink: target stored inline in i_block[] */
        memcpy(inode.i_block, target, target_len);
        inode.i_blocks = 0;
    }

    if (ext2_write_inode(fs, inode_num, &inode) != 0) {
        ext2_free_inode(fs, inode_num, 0);
        return -EIO;
    }

    if (target_len > 60) {
        /* Slow symlink: allocate a data block and write target */
        fs_node_t *lnode = ext2_alloc_node(fs, inode_num, &inode);
        if (!lnode) {
            ext2_free_inode(fs, inode_num, 0);
            return -EIO;
        }
        ext2_node_t *lctx = (ext2_node_t *)(uintptr_t)lnode->impl;
        uint32_t written = ext2_inode_write(lctx, 0, target_len, target, NULL);
        lctx->inode.i_size = target_len;
        ext2_write_inode(fs, inode_num, &lctx->inode);
        if (written < target_len) {
            /* FS-11: ext2_inode_write() may already have allocated one or
             * more data blocks for the target before it ran short.  Freeing
             * only the inode leaks those blocks, and leaving the cache slot
             * populated (fs/inode_num still set) means a later lookup of the
             * recycled inode number hits this stale entry.  Free the data
             * blocks and invalidate the slot before bailing. */
            ext2_free_inode_blocks(fs, &lctx->inode);
            ext2_free_inode(fs, inode_num, 0);
            memset(&lctx->inode, 0, sizeof(lctx->inode));
            lnode->length = 0;
            lctx->fs = NULL;
            lctx->inode_num = 0;
            return -EIO;
        }
        ext2_node_close(lnode);
    }

    if (ext2_add_entry(dir, name, inode_num, EXT2_FT_SYMLINK) != 0) {
        ext2_free_inode(fs, inode_num, 0);
        return -EIO;
    }

    return 0;
}

int ext2_mkdir(fs_node_t *dir, const char *name, uint16_t permission) {
    ext2_node_t *dir_ctx;
    ext2_fs_t *fs;
    ext2_inode_t inode;
    uint32_t inode_num;
    uint32_t block_num;
    uint8_t *block_buf = NULL;
    uint32_t now;
    uint16_t dot_len;

    /* FS-12: validate `dir` before dereferencing dir->impl. */
    if (!dir || !name || !name[0]) return -EINVAL;
    if ((dir->flags & 0x7) != FS_DIRECTORY) return -ENOTDIR;
    if (!strcmp(name, ".") || !strcmp(name, "..")) return -EINVAL;
    {
        ext2_node_t *dc = (ext2_node_t *)(uintptr_t)dir->impl;
        if (dc && EXT2_RO_REFUSE(dc->fs)) return -EROFS;
    }
    if (ext2_finddir(dir, (char *)name) != NULL) return -EEXIST;

    dir_ctx = (ext2_node_t *)(uintptr_t)dir->impl;
    fs = dir_ctx->fs;
    inode_num = ext2_alloc_inode(fs, 1);
    if (inode_num == 0) return -ENOSPC;

    block_num = ext2_alloc_block(fs);
    if (block_num == 0) {
        ext2_free_inode(fs, inode_num, 1);
        return -ENOSPC;
    }

    block_buf = kmalloc(fs->block_size);
    if (!block_buf) {
        ext2_free_block(fs, block_num);
        ext2_free_inode(fs, inode_num, 1);
        return -ENOMEM;
    }

    memset(&inode, 0, sizeof(inode));
    memset(block_buf, 0, fs->block_size);

    now = (uint32_t)get_time();

    inode.i_mode = (uint16_t)(S_IFDIR | (permission & 0777));
    inode.i_uid = dir->uid;
    inode.i_gid = dir->gid;
    inode.i_size = fs->block_size;
    inode.i_links_count = 2;
    inode.i_blocks = fs->block_size / 512;
    inode.i_block[0] = block_num;
    inode.i_atime = now;
    inode.i_mtime = now;
    inode.i_ctime = now;

    dot_len = (uint16_t)(((8 + 1 + 3) / 4) * 4);
    {
        ext2_dirent_t *dot = (ext2_dirent_t *)block_buf;
        ext2_dirent_t *dotdot = (ext2_dirent_t *)(block_buf + dot_len);

        dot->inode = inode_num;
        dot->rec_len = dot_len;
        dot->name_len = 1;
        dot->file_type = EXT2_FT_DIR;
        dot->name[0] = '.';

        dotdot->inode = dir_ctx->inode_num;
        dotdot->rec_len = (uint16_t)(fs->block_size - dot_len);
        dotdot->name_len = 2;
        dotdot->file_type = EXT2_FT_DIR;
        dotdot->name[0] = '.';
        dotdot->name[1] = '.';
    }

    if (ext2_write_block(fs, block_num, block_buf) != fs->block_size) {
        kfree(block_buf, fs->block_size);
        ext2_free_block(fs, block_num);
        ext2_free_inode(fs, inode_num, 1);
        return -EIO;
    }
    kfree(block_buf, fs->block_size);

    if (ext2_write_inode(fs, inode_num, &inode) != 0) {
        ext2_free_block(fs, block_num);
        ext2_free_inode(fs, inode_num, 1);
        return -EIO;
    }

    if (ext2_add_entry(dir, name, inode_num, EXT2_FT_DIR) != 0) {
        ext2_free_inode_blocks(fs, &inode);
        ext2_free_inode(fs, inode_num, 1);
        return -EIO;
    }

    dir_ctx->inode.i_links_count++;
    dir_ctx->inode.i_mtime = now;
    dir_ctx->inode.i_ctime = now;
    if (ext2_write_inode(fs, dir_ctx->inode_num, &dir_ctx->inode) != 0) {
        ext2_remove_entry(dir, name);
        ext2_free_inode_blocks(fs, &inode);
        ext2_free_inode(fs, inode_num, 1);
        dir_ctx->inode.i_links_count--;
        return -EIO;
    }

    return 0;
}

int ext2_unlink(fs_node_t *dir, const char *name) {
    fs_node_t *victim;
    ext2_node_t *victim_ctx;
    ext2_fs_t *fs;
    int ret;

    /* FS-12: validate `dir` before dereferencing dir->impl (the read-only
     * refusal probe below reads it). */
    if (!dir || !name || !name[0]) return -EINVAL;
    if ((dir->flags & 0x7) != FS_DIRECTORY) return -ENOTDIR;
    {
        ext2_node_t *dc = (ext2_node_t *)(uintptr_t)dir->impl;
        if (dc && EXT2_RO_REFUSE(dc->fs)) return -EROFS;
    }

    victim = ext2_finddir(dir, (char *)name);
    if (!victim) return -ENOENT;
    if ((victim->flags & 0x7) == FS_DIRECTORY) return -EISDIR;

    victim_ctx = (ext2_node_t *)(uintptr_t)victim->impl;
    fs = victim_ctx->fs;

    ret = ext2_remove_entry(dir, name);
    if (ret != 0) return -EIO;

    if (victim_ctx->inode.i_links_count > 0) {
        victim_ctx->inode.i_links_count--;
    }

    if (victim_ctx->inode.i_links_count == 0) {
        victim_ctx->inode.i_dtime = (uint32_t)get_time();
        if (victim_ctx->pin_count > 0) {
            /* POSIX unlink-while-open semantics: the directory entry
             * is gone (already removed above) so the file is no
             * longer reachable by name, but the inode and its data
             * blocks must remain valid for the open FDs.  Mark the
             * in-memory state as orphaned; ext2_node_close will free
             * the blocks and inode once pin_count drops to 0. */
            victim_ctx->orphaned = 1;
            victim_ctx->was_dir_at_unlink = 0;
            if (ext2_write_inode(fs, victim_ctx->inode_num, &victim_ctx->inode) != 0) {
                return -EIO;
            }
        } else {
            ret = ext2_free_inode_blocks(fs, &victim_ctx->inode);
            if (ret != 0) return ret;
            if (ext2_write_inode(fs, victim_ctx->inode_num, &victim_ctx->inode) != 0) {
                return -EIO;
            }
            ext2_free_inode(fs, victim_ctx->inode_num, 0);
            memset(&victim_ctx->inode, 0, sizeof(victim_ctx->inode));
            victim->length = 0;
            /* Invalidate the cache slot — see matching comment in
             * ext2_node_close.  Same hazard: inode_num gets reused
             * fast and the next finddir for it would otherwise hit
             * the stale slot. */
            victim_ctx->fs = NULL;
            victim_ctx->inode_num = 0;
        }
    } else {
        /* Update ctime: link count changed */
        victim_ctx->inode.i_ctime = (uint32_t)get_time();
        if (ext2_write_inode(fs, victim_ctx->inode_num, &victim_ctx->inode) != 0) {
            return -EIO;
        }
    }

    return 0;
}

static int ext2_dir_is_empty(fs_node_t *node) {
    uint64_t off = 0;
    struct dirent *de;

    if (!node || (node->flags & 0x7) != FS_DIRECTORY) return 0;

    /* ext2_readdir takes an opaque BYTE OFFSET and reports the next one in
     * d_off; advance via that, not a ++ entry counter (a bare increment lands
     * mid-record and misparses, making a truly empty dir look non-empty and
     * breaking rmdir/rm -rf). */
    while ((de = ext2_readdir(node, off)) != NULL) {
        off = (de->d_off > off) ? de->d_off : off + 1;
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
            continue;
        }
        return 0;
    }
    return 1;
}

int ext2_rmdir(fs_node_t *dir, const char *name) {
    fs_node_t *victim;
    ext2_node_t *dir_ctx;
    ext2_node_t *victim_ctx;
    ext2_fs_t *fs;
    int ret;

    /* FS-12: validate `dir` before dereferencing dir->impl. */
    if (!dir || !name || !name[0]) return -EINVAL;
    if ((dir->flags & 0x7) != FS_DIRECTORY) return -ENOTDIR;
    if (!strcmp(name, ".") || !strcmp(name, "..")) return -EINVAL;
    {
        ext2_node_t *dc = (ext2_node_t *)(uintptr_t)dir->impl;
        if (dc && EXT2_RO_REFUSE(dc->fs)) return -EROFS;
    }

    victim = ext2_finddir(dir, (char *)name);
    if (!victim) return -ENOENT;
    if ((victim->flags & 0x7) != FS_DIRECTORY) return -ENOTDIR;
    if (!ext2_dir_is_empty(victim)) return -ENOTEMPTY;

    dir_ctx = (ext2_node_t *)(uintptr_t)dir->impl;
    victim_ctx = (ext2_node_t *)(uintptr_t)victim->impl;
    fs = victim_ctx->fs;

    ret = ext2_remove_entry(dir, name);
    if (ret != 0) return -EIO;

    if (dir_ctx->inode.i_links_count > 0) {
        dir_ctx->inode.i_links_count--;
    }

    dir_ctx->inode.i_mtime = (uint32_t)get_time();
    dir_ctx->inode.i_ctime = dir_ctx->inode.i_mtime;
    if (ext2_write_inode(fs, dir_ctx->inode_num, &dir_ctx->inode) != 0) {
        return -EIO;
    }

    victim_ctx->inode.i_links_count = 0;
    victim_ctx->inode.i_dtime = (uint32_t)get_time();

    /*
     * EXT2-16: this used to free the blocks and the inode unconditionally,
     * with no equivalent of the unlink-while-open handling a few functions
     * up.  An open DIR fd (opendir() holds the node pinned) therefore went on
     * reading directory blocks that were already back on the free list and
     * could have been handed to another file -- the readdir loop would walk
     * whatever landed there.  POSIX requires rmdir of a directory with open
     * references to unlink the name now and defer the inode teardown, exactly
     * as unlink does.  ext2_node_close() already knows how to finish a
     * deferred delete for a directory (was_dir_at_unlink picks the is_dir
     * argument to ext2_free_inode); rmdir simply never set it up.
     */
    if (victim_ctx->pin_count > 0) {
        victim_ctx->orphaned = 1;
        victim_ctx->was_dir_at_unlink = 1;
        if (ext2_write_inode(fs, victim_ctx->inode_num, &victim_ctx->inode) != 0)
            return -EIO;
        return 0;
    }

    ret = ext2_free_inode_blocks(fs, &victim_ctx->inode);
    if (ret != 0) return ret;
    if (ext2_write_inode(fs, victim_ctx->inode_num, &victim_ctx->inode) != 0) {
        return -EIO;
    }
    ext2_free_inode(fs, victim_ctx->inode_num, 1);
    memset(&victim_ctx->inode, 0, sizeof(victim_ctx->inode));
    victim->length = 0;
    /* Invalidate the cache slot, as ext2_unlink does: inode numbers get
     * reused quickly, and a later finddir for this one would otherwise hit
     * this stale slot holding a zeroed inode. */
    victim_ctx->fs = NULL;
    victim_ctx->inode_num = 0;

    return 0;
}

int ext2_statfs(fs_node_t *node, struct statfs *buf) {
    if (!node || !buf) return -EINVAL;
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    if (!ctx) return -EINVAL;
    ext2_fs_t *fs = ctx->fs;

    buf->f_type         = EXT2_SUPER_MAGIC;
    buf->f_bsize        = fs->block_size;
    buf->f_iosize       = fs->block_size;
    buf->f_blocks       = fs->sb.s_blocks_count;
    buf->f_bfree        = fs->sb.s_free_blocks_count;
    buf->f_bavail       = fs->sb.s_free_blocks_count;
    buf->f_files        = fs->sb.s_inodes_count;
    buf->f_ffree        = fs->sb.s_free_inodes_count;
    buf->f_fsid         = 0;
    buf->f_owner        = 0;
    buf->f_flags        = 0;
    buf->f_syncwrites   = 0;
    buf->f_asyncwrites  = 0;
    strncpy(buf->f_fstypename, "ext2", sizeof(buf->f_fstypename));
    memset(buf->f_mntonname, 0, sizeof(buf->f_mntonname));
    memset(buf->f_mntfromname, 0, sizeof(buf->f_mntfromname));

    return 0;
}
/* MNT_UPDATE hook: flip the live mount between read-only and read-write.
 * Refuses a read-write remount if the volume was forced read-only for
 * unsupported RO_COMPAT features (writing could corrupt it). */
int ext2_remount(fs_node_t *node, uint32_t flags) {
    if (!node) return -EINVAL;
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    if (!ctx || !ctx->fs) return -EINVAL;
    ext2_fs_t *fs = ctx->fs;

    int want_ro = !!(flags & MNT_RDONLY);
    if (!want_ro && fs->force_readonly)
        return -EROFS;          /* cannot safely write this volume */
    fs->readonly = want_ro;
    fs->mnt_flags = flags;
    return 0;
}

int ext2_unmount(fs_node_t *node) {
    if (!node) return -EINVAL;
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    if (!ctx) return -EINVAL;
    ext2_fs_t *fs = ctx->fs;
    if (!fs) return -EINVAL;

    // Free all active cached nodes for this fs
    for (int i = 0; i < EXT2_NODE_CACHE_SIZE; i++) {
        if (ext2_node_cache[i].fs == fs) {
            uint32_t old_block_size = fs->block_size;
            if (ext2_node_cache[i].block_buf) kfree(ext2_node_cache[i].block_buf, old_block_size);
            if (ext2_node_cache[i].indirect_buf) kfree(ext2_node_cache[i].indirect_buf, old_block_size);
            if (ext2_node_cache[i].dindirect_buf) kfree(ext2_node_cache[i].dindirect_buf, old_block_size);
            if (ext2_node_cache[i].tindirect_buf) kfree(ext2_node_cache[i].tindirect_buf, old_block_size);
            memset(&ext2_node_cache[i], 0, sizeof(ext2_node_t));
            memset(&ext2_fs_node_cache[i], 0, sizeof(fs_node_t));
        }
    }

    /* Reconcile any deferred superblock / group-descriptor free counts to
     * disk before tearing the in-core copies down. */
    ext2_sync_meta(fs);

    if (fs->active_bg_bitmap) kfree(fs->active_bg_bitmap, fs->block_size);
    if (fs->active_inode_bg_bitmap) kfree(fs->active_inode_bg_bitmap, fs->block_size);
    if (fs->bgd) kfree(fs->bgd, fs->group_count * sizeof(ext2_group_desc_t));
    if (fs->bgd_dirty) {
        uint32_t sz = (fs->group_count + 7u) / 8u;
        if (sz == 0) sz = 1;
        kfree(fs->bgd_dirty, sz);
    }

    kfree(fs, sizeof(ext2_fs_t));
    return 0;
}
