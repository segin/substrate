/*
 * exFAT filesystem driver (read-only).
 *
 * Mounts an exFAT volume from a block device and provides directory
 * traversal, file read, statfs and volume-label probing.  The driver follows
 * cluster chains via the FAT, honouring the per-file "NoFatChain" flag that
 * marks contiguously-allocated data (the common case), and parses exFAT's
 * multi-entry directory sets (0x85 file entry + 0xC0 stream + 0xC1 name
 * entries) with UTF-16LE -> UTF-8 name conversion.
 *
 * Not implemented: writes (allocation bitmap / up-case table / set-checksum
 * maintenance) and the full up-case table for name comparison (finddir folds
 * ASCII case only, which covers the common case; non-ASCII names still read
 * and list correctly, they just aren't case-folded on lookup).
 */
#include <string.h>

#include <drivers/storage/blkdev.h>
#include <fs/exfat/exfat.h>
#include <kern/console.h>
#include <kern/time.h>
#include <sys/dirent.h>
#include <sys/errno.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <vm/vm_kmem.h>

#define EXFAT_ROOT_INO         1ULL
#define EXFAT_MAX_DIR_ENTRIES  (1u << 22)   /* runaway-chain guard */
#define EXFAT_MAX_CLUSTER_SIZE (1u << 25)   /* exFAT caps clusters at 32 MiB */

/*
 * Node cache: a fixed slot table shared by all exFAT mounts, mirroring the
 * FAT driver.  finddir() hands out short-lived nodes; the VFS uses them
 * immediately (open/read/stat) so a round-robin ring is sufficient.
 */
static exfat_node_t exfat_node_cache[EXFAT_NODE_CACHE_SIZE];
static fs_node_t    exfat_fs_node_cache[EXFAT_NODE_CACHE_SIZE];
static uint32_t     exfat_node_cache_idx;
/* exFAT-F3: guards slot selection, pin counts, node population and the
 * re-populating memset.  Initialised once in exfat_init() (single-threaded at
 * driver registration) — the old lazy "ensure" ran mutex_init() with no
 * protecting lock, so two concurrent first-ever mounts double-initialised it. */
static mutex_t      exfat_node_cache_lock;

static void exfat_free_chain(exfat_fs_t *fs, uint32_t start, int no_fat_chain,
                             uint64_t nbytes);
static int  exfat_cluster_valid(exfat_fs_t *fs, uint32_t cluster);
static uint32_t exfat_le32(const uint8_t *p);
static uint64_t exfat_le64(const uint8_t *p);

static void exfat_node_open(fs_node_t *node) {
    exfat_node_t *ctx = node ? (exfat_node_t *)(uintptr_t)node->impl : NULL;
    if (!ctx) return;
    mutex_lock(&exfat_node_cache_lock);
    ctx->pin++;
    mutex_unlock(&exfat_node_cache_lock);
}

static void exfat_node_close(fs_node_t *node) {
    exfat_node_t *ctx = node ? (exfat_node_t *)(uintptr_t)node->impl : NULL;
    if (!ctx) return;
    mutex_lock(&exfat_node_cache_lock);
    if (ctx->pin > 0) ctx->pin--;
    /* audit H6: a file unlinked while open kept its chain via ctx->orphaned;
     * free it now that the last reference has dropped.  Re-pin across the
     * teardown (which does device I/O under fs->lock, not the cache lock) so
     * the allocator cannot hand this half-freed slot to another lookup. */
    int finish = (ctx->pin == 0 && ctx->orphaned && ctx->fs);
    if (finish) ctx->pin = 1;
    mutex_unlock(&exfat_node_cache_lock);
    if (finish) {
        exfat_fs_t *fs = ctx->fs;
        mutex_lock(&fs->lock);
        if (exfat_cluster_valid(fs, ctx->orphan_first))
            exfat_free_chain(fs, ctx->orphan_first, ctx->orphan_nfc, ctx->orphan_size);
        mutex_unlock(&fs->lock);
        mutex_lock(&exfat_node_cache_lock);
        ctx->orphaned = 0;
        ctx->orphan_first = 0;
        ctx->pin = 0;
        mutex_unlock(&exfat_node_cache_lock);
    }
}

/*
 * audit H6: if the file (fs, inode) is currently open, mark its cached node
 * orphaned so its cluster chain is freed on the last close, and return 1.
 * Otherwise return 0 and let the caller free the chain now.  The caller holds
 * fs->lock; the orphan fields are stamped atomically under the cache lock.
 */
static int exfat_defer_or_free(exfat_fs_t *fs, uint64_t inode,
                               uint32_t first, int nfc, uint64_t size) {
    int deferred = 0;
    mutex_lock(&exfat_node_cache_lock);
    for (uint32_t i = 0; i < EXFAT_NODE_CACHE_SIZE; i++) {
        if (exfat_node_cache[i].fs == fs && exfat_node_cache[i].pin > 0 &&
            exfat_fs_node_cache[i].inode == inode) {
            exfat_node_cache[i].orphaned     = 1;
            exfat_node_cache[i].orphan_first = first;
            exfat_node_cache[i].orphan_nfc   = (uint8_t)nfc;
            exfat_node_cache[i].orphan_size  = size;
            deferred = 1;
            break;
        }
    }
    mutex_unlock(&exfat_node_cache_lock);
    return deferred;
}

/*
 * audit H6: after rename relocates a file's directory entry, update any open
 * cached node so its recorded entry location (and inode) follow the move -- a
 * stale open fd would otherwise write through the old, now-deleted entry.
 * Caller holds fs->lock.
 */
static void exfat_relocate_cached(exfat_fs_t *fs, uint64_t old_inode,
                                  uint32_t new_dir_cluster, uint8_t new_dir_nfc,
                                  uint64_t new_primary, uint8_t new_secondary,
                                  uint64_t new_inode) {
    mutex_lock(&exfat_node_cache_lock);
    for (uint32_t i = 0; i < EXFAT_NODE_CACHE_SIZE; i++) {
        if (exfat_node_cache[i].fs == fs && exfat_node_cache[i].pin > 0 &&
            exfat_fs_node_cache[i].inode == old_inode) {
            exfat_node_cache[i].dir_cluster     = new_dir_cluster;
            exfat_node_cache[i].dir_no_fat_chain = new_dir_nfc;
            exfat_node_cache[i].primary_index   = new_primary;
            exfat_node_cache[i].secondary_count = new_secondary;
            exfat_fs_node_cache[i].inode        = new_inode;
            break;
        }
    }
    mutex_unlock(&exfat_node_cache_lock);
}

/* forward decls */
static struct dirent *exfat_readdir(fs_node_t *node, uint64_t index);
static fs_node_t     *exfat_finddir(fs_node_t *node, char *name);
static size_t         exfat_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer);
static size_t         exfat_file_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer);
static int            exfat_truncate(fs_node_t *node, off_t new_size);
static int            exfat_mkdir(fs_node_t *parent, const char *name, uint16_t perm);
static int            exfat_mknod(fs_node_t *parent, const char *name, uint16_t mode, uint32_t dev);
static int            exfat_unlink(fs_node_t *parent, const char *name);
static int            exfat_rmdir(fs_node_t *parent, const char *name);
static int            exfat_rename(fs_node_t *old_parent, const char *old_name,
                                   fs_node_t *new_parent, const char *new_name);
static int            exfat_statfs(fs_node_t *node, struct statfs *buf);
static int            exfat_unmount(fs_node_t *root);

/* Per-mount-lock-holding implementations behind the public op wrappers below. */
static size_t         exfat_file_write_locked(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer);
static int            exfat_truncate_locked(fs_node_t *node, off_t new_size);
static int            exfat_mkdir_locked(fs_node_t *parent, const char *name, uint16_t perm);
static int            exfat_mknod_locked(fs_node_t *parent, const char *name, uint16_t mode, uint32_t dev);
static int            exfat_unlink_locked(fs_node_t *parent, const char *name);
static int            exfat_rmdir_locked(fs_node_t *parent, const char *name);
static int            exfat_rename_locked(fs_node_t *old_parent, const char *old_name,
                                          fs_node_t *new_parent, const char *new_name);

/* Fetch the owning mount from a node (NULL-safe). */
static exfat_fs_t *exfat_node_fs(fs_node_t *node) {
    exfat_node_t *ctx = node ? (exfat_node_t *)(uintptr_t)node->impl : NULL;
    return ctx ? ctx->fs : NULL;
}

/*
 * Public mutation ops: take the per-mount lock (audit H3) so allocator +
 * directory-slot assignment + FAT/bitmap RMW never interleave between two
 * writers, then delegate to the *_locked body.  The bodies and every helper
 * they call assume the lock is already held and never re-take it.
 */
static size_t exfat_file_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    exfat_fs_t *fs = exfat_node_fs(node);
    if (!fs) return 0;
    mutex_lock(&fs->lock);
    size_t r = exfat_file_write_locked(node, offset, size, buffer);
    mutex_unlock(&fs->lock);
    return r;
}
static int exfat_truncate(fs_node_t *node, off_t new_size) {
    exfat_fs_t *fs = exfat_node_fs(node);
    if (!fs) return -EINVAL;
    mutex_lock(&fs->lock);
    int r = exfat_truncate_locked(node, new_size);
    mutex_unlock(&fs->lock);
    return r;
}
static int exfat_mkdir(fs_node_t *parent, const char *name, uint16_t perm) {
    exfat_fs_t *fs = exfat_node_fs(parent);
    if (!fs) return -EINVAL;
    mutex_lock(&fs->lock);
    int r = exfat_mkdir_locked(parent, name, perm);
    mutex_unlock(&fs->lock);
    return r;
}
static int exfat_mknod(fs_node_t *parent, const char *name, uint16_t mode, uint32_t dev) {
    exfat_fs_t *fs = exfat_node_fs(parent);
    if (!fs) return -EINVAL;
    mutex_lock(&fs->lock);
    int r = exfat_mknod_locked(parent, name, mode, dev);
    mutex_unlock(&fs->lock);
    return r;
}
static int exfat_unlink(fs_node_t *parent, const char *name) {
    exfat_fs_t *fs = exfat_node_fs(parent);
    if (!fs) return -EINVAL;
    mutex_lock(&fs->lock);
    int r = exfat_unlink_locked(parent, name);
    mutex_unlock(&fs->lock);
    return r;
}
static int exfat_rmdir(fs_node_t *parent, const char *name) {
    exfat_fs_t *fs = exfat_node_fs(parent);
    if (!fs) return -EINVAL;
    mutex_lock(&fs->lock);
    int r = exfat_rmdir_locked(parent, name);
    mutex_unlock(&fs->lock);
    return r;
}
static int exfat_rename(fs_node_t *old_parent, const char *old_name,
                        fs_node_t *new_parent, const char *new_name) {
    /* VFS guarantees rename is intra-mount, so both dirs share one lock. */
    exfat_fs_t *fs = exfat_node_fs(old_parent);
    if (!fs) return -EINVAL;
    mutex_lock(&fs->lock);
    int r = exfat_rename_locked(old_parent, old_name, new_parent, new_name);
    mutex_unlock(&fs->lock);
    return r;
}

/* ---------------------------------------------------------------- device I/O */

static int exfat_read_bytes(exfat_fs_t *fs, uint64_t byte_off, uint32_t size, void *buf) {
    if (!fs || !fs->device || !fs->device->read || !buf) return -1;
    size_t got = fs->device->read(fs->device, (off_t)byte_off, size, (uint8_t *)buf);
    return (got == (size_t)size) ? 0 : -1;
}

static int exfat_read_sectors(exfat_fs_t *fs, uint64_t sector, uint32_t count, void *buf) {
    return exfat_read_bytes(fs, sector * fs->bytes_per_sector,
                            count * fs->bytes_per_sector, buf);
}

static int exfat_cluster_valid(exfat_fs_t *fs, uint32_t cluster) {
    return cluster >= EXFAT_FIRST_CLUSTER &&
           cluster < EXFAT_FIRST_CLUSTER + fs->cluster_count;
}

/* First sector of a data cluster (cluster numbering starts at 2). */
static uint64_t exfat_cluster_sector(exfat_fs_t *fs, uint32_t cluster) {
    return fs->cluster_heap_offset +
           ((uint64_t)(cluster - EXFAT_FIRST_CLUSTER) << fs->sectors_per_cluster_shift);
}

static int exfat_read_cluster(exfat_fs_t *fs, uint32_t cluster, void *buf) {
    if (!exfat_cluster_valid(fs, cluster)) return -1;
    return exfat_read_sectors(fs, exfat_cluster_sector(fs, cluster),
                              fs->sectors_per_cluster, buf);
}

/* FAT lookup: next cluster in the chain (>= EXFAT_CLUSTER_END at the end). */
static uint32_t exfat_fat_next(exfat_fs_t *fs, uint32_t cluster) {
    if (!exfat_cluster_valid(fs, cluster)) return 0xFFFFFFFFU;
    uint64_t off = fs->fat_offset * fs->bytes_per_sector + (uint64_t)cluster * 4;
    uint32_t next = 0xFFFFFFFFU;
    if (exfat_read_bytes(fs, off, 4, &next) != 0) return 0xFFFFFFFFU;
    return next;   /* on-disk FAT is little-endian, matching x86 */
}

/*
 * Return the cluster number `n` steps along a chain that starts at `start`,
 * or 0 if that is past the end.  Contiguous (NoFatChain) files skip the FAT.
 *
 * audit M2/M8: `n` is 64-bit (callers derive it from a 64-bit byte offset, so a
 * 32-bit parameter truncated the index and returned a valid-looking WRONG
 * cluster).  A valid chain spans at most ClusterCount clusters, so any index
 * that far out is past the end -- this both rejects the overflowed offset and
 * caps the FAT walk (a cyclic FAT can never make the loop exceed ClusterCount).
 */
static uint32_t exfat_chain_nth(exfat_fs_t *fs, uint32_t start, int no_fat_chain, uint64_t n) {
    if (!exfat_cluster_valid(fs, start)) return 0;
    if (n >= (uint64_t)fs->cluster_count) return 0;
    if (no_fat_chain) {
        uint64_t c = (uint64_t)start + n;
        if (c > 0xFFFFFFFFULL || !exfat_cluster_valid(fs, (uint32_t)c)) return 0;
        return (uint32_t)c;
    }
    uint32_t c = start;
    for (uint64_t i = 0; i < n; i++) {
        uint32_t nx = exfat_fat_next(fs, c);
        if (nx < EXFAT_FIRST_CLUSTER || nx >= EXFAT_CLUSTER_END) return 0;
        c = nx;
    }
    return c;
}

/* -------------------------------------------------------------- device write */

static int exfat_write_bytes(exfat_fs_t *fs, uint64_t byte_off, uint32_t size,
                             const void *buf) {
    if (!fs || !fs->device || !fs->device->write || !buf) return -1;
    /* The block layer handles unaligned / sub-sector writes via RMW. */
    size_t wrote = fs->device->write(fs->device, (off_t)byte_off, size,
                                     (const uint8_t *)buf);
    return (wrote == (size_t)size) ? 0 : -1;
}

static int exfat_write_cluster(exfat_fs_t *fs, uint32_t cluster, const void *buf) {
    if (!exfat_cluster_valid(fs, cluster)) return -1;
    return exfat_write_bytes(fs, exfat_cluster_sector(fs, cluster) * fs->bytes_per_sector,
                             fs->cluster_size, buf);
}

/* Set FAT[cluster] = value (on-disk, little-endian).  Updates every FAT copy
 * is unnecessary — exFAT defines a single active FAT (num_fats is 1 except on
 * TexFAT); we write the active FAT at fat_offset. */
static int exfat_fat_set(exfat_fs_t *fs, uint32_t cluster, uint32_t value) {
    if (!exfat_cluster_valid(fs, cluster)) return -1;
    uint64_t off = fs->fat_offset * fs->bytes_per_sector + (uint64_t)cluster * 4;
    return exfat_write_bytes(fs, off, 4, &value);
}

/* ---------------------------------------------------------- allocation bitmap */

/* Count the set bits in a byte (freestanding: no libgcc __popcountsi2). */
static inline uint32_t exfat_popcount8(uint8_t x) {
    x = (uint8_t)(x - ((x >> 1) & 0x55));
    x = (uint8_t)((x & 0x33) + ((x >> 2) & 0x33));
    return (uint32_t)((x + (x >> 4)) & 0x0F);
}

/* In-memory bitmap test: bit i corresponds to cluster i + 2. */
static int exfat_bitmap_test(exfat_fs_t *fs, uint32_t cluster) {
    uint32_t bit = cluster - EXFAT_FIRST_CLUSTER;
    if (!fs->bitmap || (bit >> 3) >= fs->bitmap_bytes) return 1; /* treat as used */
    return (fs->bitmap[bit >> 3] >> (bit & 7)) & 1;
}

/* Flush the single bitmap byte holding `bit` back to its on-disk location,
 * walking the bitmap's own cluster chain to find the containing cluster. */
static int exfat_bitmap_flush_bit(exfat_fs_t *fs, uint32_t bit) {
    uint32_t byte_idx = bit >> 3;
    if (byte_idx >= fs->bitmap_bytes) return -1;
    uint32_t within_cluster = byte_idx % fs->cluster_size;
    uint32_t cluster_idx = byte_idx / fs->cluster_size;
    uint32_t c = exfat_chain_nth(fs, fs->bitmap_cluster, fs->bitmap_no_fat_chain,
                                 cluster_idx);
    if (c == 0) return -1;
    uint64_t off = exfat_cluster_sector(fs, c) * fs->bytes_per_sector + within_cluster;
    return exfat_write_bytes(fs, off, 1, &fs->bitmap[byte_idx]);
}

/* Mark a cluster used/free in memory and flush the affected byte.
 * audit M6: apply the in-memory bit and free_clusters accounting only after the
 * flush succeeds -- on a flush failure the byte is reverted to its prior value
 * so the in-memory bitmap can't drift out of sync with the disk. */
static int exfat_bitmap_set(exfat_fs_t *fs, uint32_t cluster, int used) {
    uint32_t bit = cluster - EXFAT_FIRST_CLUSTER;
    if (!fs->bitmap || (bit >> 3) >= fs->bitmap_bytes) return -1;
    uint8_t *b = &fs->bitmap[bit >> 3];
    uint8_t mask = (uint8_t)(1u << (bit & 7));
    int was = (*b & mask) ? 1 : 0;
    if (used) *b |= mask;
    else      *b &= (uint8_t)~mask;
    if (exfat_bitmap_flush_bit(fs, bit) != 0) {
        if (was) *b |= mask; else *b &= (uint8_t)~mask;   /* revert to prior */
        return -1;
    }
    if (used && !was && fs->free_clusters) fs->free_clusters--;
    if (!used && was) fs->free_clusters++;
    return 0;
}

/*
 * Allocate the first free cluster (scanning from cluster 2), mark it used in
 * the bitmap (flushed to disk) and terminate its FAT entry with EOF so it is a
 * valid one-cluster chain.  Returns the cluster number, or 0 on failure.
 */
static uint32_t exfat_alloc_cluster(exfat_fs_t *fs) {
    if (!fs->bitmap) return 0;
    for (uint32_t c = EXFAT_FIRST_CLUSTER;
         c < EXFAT_FIRST_CLUSTER + fs->cluster_count; c++) {
        if (!exfat_bitmap_test(fs, c)) {
            /* audit I2: never hand out a cluster the FAT marks BAD even if the
             * bitmap bit is (inconsistently) clear -- reserve it in memory and
             * keep scanning. */
            if (exfat_fat_next(fs, c) == EXFAT_CLUSTER_BAD) {
                exfat_bitmap_set(fs, c, 1);
                continue;
            }
            if (exfat_bitmap_set(fs, c, 1) != 0) return 0;
            if (exfat_fat_set(fs, c, EXFAT_CLUSTER_EOF) != 0) {
                exfat_bitmap_set(fs, c, 0);
                return 0;
            }
            return c;
        }
    }
    return 0;
}

/*
 * audit H7: allocate a data cluster and zero its contents before it becomes
 * part of a file's readable range.  exfat_alloc_cluster hands back a cluster
 * still holding a previously-deleted file's data; without this a hole created
 * by a sparse write / truncate-grow, or the unwritten tail of the last cluster,
 * would expose that freed residue instead of the zeroes §7.6.5 requires.
 * Returns 0 (and frees the cluster) on any failure.
 */
static void exfat_free_cluster(exfat_fs_t *fs, uint32_t cluster);

static uint32_t exfat_alloc_cluster_zeroed(exfat_fs_t *fs) {
    uint32_t c = exfat_alloc_cluster(fs);
    if (c == 0) return 0;
    uint8_t *z = kmalloc(fs->cluster_size);
    if (!z) { exfat_free_cluster(fs, c); return 0; }
    memset(z, 0, fs->cluster_size);
    int rc = exfat_write_cluster(fs, c, z);
    kfree(z, fs->cluster_size);
    if (rc != 0) { exfat_free_cluster(fs, c); return 0; }
    return c;
}

/* Free a single cluster: clear its bitmap bit (flushed) and zero its FAT entry. */
static void exfat_free_cluster(exfat_fs_t *fs, uint32_t cluster) {
    if (!exfat_cluster_valid(fs, cluster)) return;
    exfat_fat_set(fs, cluster, 0);
    exfat_bitmap_set(fs, cluster, 0);
}

/* Free a whole FAT cluster chain (or a contiguous run for NoFatChain). */
static void exfat_free_chain(exfat_fs_t *fs, uint32_t start, int no_fat_chain,
                             uint64_t nbytes) {
    if (!exfat_cluster_valid(fs, start)) return;
    if (no_fat_chain) {
        uint32_t n = (uint32_t)((nbytes + fs->cluster_size - 1) / fs->cluster_size);
        for (uint32_t i = 0; i < n; i++)
            exfat_free_cluster(fs, start + i);
        return;
    }
    uint32_t c = start;
    uint32_t guard = 0;
    while (exfat_cluster_valid(fs, c) && guard++ < fs->cluster_count) {
        /* audit I3: a corrupt back-linked chain revisits a cluster we already
         * freed (its bit is now clear) -- stop rather than touch it twice. */
        if (!exfat_bitmap_test(fs, c)) break;
        uint32_t nx = exfat_fat_next(fs, c);
        exfat_free_cluster(fs, c);
        if (nx < EXFAT_FIRST_CLUSTER || nx >= EXFAT_CLUSTER_END) break;
        c = nx;
    }
}

/* ------------------------------------------------------------- name / time */

/*
 * Convert up to `nchars` UTF-16LE code units to UTF-8 into `dst` (`dstsz`
 * includes the NUL).  Surrogate pairs are decoded; a lone surrogate is
 * emitted as its raw code point.  Returns bytes written (excluding NUL).
 */
static size_t exfat_utf16_to_utf8(const uint8_t *src, int nchars, char *dst, size_t dstsz) {
    size_t o = 0;
    if (dstsz == 0) return 0;
    for (int i = 0; i < nchars && o + 1 < dstsz; i++) {
        /* Read code units as explicit little-endian bytes so the source may be
         * an unaligned / packed on-disk field. */
        uint32_t cp = (uint32_t)src[2 * i] | ((uint32_t)src[2 * i + 1] << 8);
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < nchars) {
            uint32_t lo = (uint32_t)src[2 * (i + 1)] | ((uint32_t)src[2 * (i + 1) + 1] << 8);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                i++;
            }
        }
        if (cp < 0x80) {
            dst[o++] = (char)cp;
        } else if (cp < 0x800) {
            if (o + 2 >= dstsz) break;
            dst[o++] = (char)(0xC0 | (cp >> 6));
            dst[o++] = (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            if (o + 3 >= dstsz) break;
            dst[o++] = (char)(0xE0 | (cp >> 12));
            dst[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            dst[o++] = (char)(0x80 | (cp & 0x3F));
        } else {
            if (o + 4 >= dstsz) break;
            dst[o++] = (char)(0xF0 | (cp >> 18));
            dst[o++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            dst[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            dst[o++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    dst[o] = '\0';
    return o;
}

/*
 * Convert a UTF-8 string to UTF-16 code units (BMP + surrogate pairs).  Writes
 * up to `cap` code units and returns the count.  Used to build on-disk name
 * entries and to fold a lookup name for comparison.
 */
static int exfat_utf8_to_utf16(const char *s, uint16_t *out, int cap) {
    int n = 0;
    const uint8_t *p = (const uint8_t *)s;
    while (*p && n < cap) {
        uint32_t cp;
        if (p[0] < 0x80) { cp = p[0]; p += 1; }
        else if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
            cp = ((uint32_t)(p[0] & 0x1F) << 6) | (p[1] & 0x3F); p += 2;
        } else if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 &&
                   (p[2] & 0xC0) == 0x80) {
            cp = ((uint32_t)(p[0] & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) |
                 (p[2] & 0x3F); p += 3;
        } else if ((p[0] & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 &&
                   (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
            cp = ((uint32_t)(p[0] & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) |
                 ((uint32_t)(p[2] & 0x3F) << 6) | (p[3] & 0x3F); p += 4;
        } else { cp = p[0]; p += 1; }   /* invalid byte: pass through */

        if (cp >= 0x10000) {
            cp -= 0x10000;
            if (n + 2 > cap) break;
            out[n++] = (uint16_t)(0xD800 | (cp >> 10));
            out[n++] = (uint16_t)(0xDC00 | (cp & 0x3FF));
        } else {
            out[n++] = (uint16_t)cp;
        }
    }
    return n;
}

/* Up-case one UTF-16 code unit through the mount's table (identity fallback). */
static uint16_t exfat_upcase(exfat_fs_t *fs, uint16_t c) {
    return (fs && fs->upcase) ? fs->upcase[c] : c;
}

/*
 * NameHash over an already up-cased UTF-16LE name: the exFAT 16-bit
 * rotate-right-1 + add algorithm run over each byte (low then high).
 */
static uint16_t exfat_name_hash(exfat_fs_t *fs, const uint16_t *name, int nchars) {
    uint16_t h = 0;
    for (int i = 0; i < nchars; i++) {
        uint16_t u = exfat_upcase(fs, name[i]);
        uint8_t bytes[2] = { (uint8_t)(u & 0xFF), (uint8_t)(u >> 8) };
        for (int b = 0; b < 2; b++) {
            h = (uint16_t)(((h << 15) | (h >> 1)) + bytes[b]);
        }
    }
    return h;
}

/*
 * SetChecksum over the concatenated 32-byte entries of a whole set, skipping
 * the two SetChecksum bytes (offsets 2 and 3 of the primary entry).
 */
static uint16_t exfat_set_checksum(const uint8_t *entries, uint32_t nbytes) {
    uint16_t c = 0;
    for (uint32_t i = 0; i < nbytes; i++) {
        if (i == 2 || i == 3) continue;
        c = (uint16_t)(((c << 15) | (c >> 1)) + entries[i]);
    }
    return c;
}

/* Up-case-folded equality of a UTF-16 name against a UTF-8 lookup name. */
static int exfat_name_eq_utf16(exfat_fs_t *fs, const uint16_t *disk, int dlen,
                               const char *want) {
    uint16_t w[256];
    int wlen = exfat_utf8_to_utf16(want, w, 255);
    if (wlen != dlen) return 0;
    for (int i = 0; i < dlen; i++)
        if (exfat_upcase(fs, disk[i]) != exfat_upcase(fs, w[i])) return 0;
    return 1;
}

/*
 * exFAT timestamp -> seconds since the Unix epoch (interpreted as UTC; the
 * per-field UtcOffset byte is ignored).  Layout: bits 0-4 seconds/2, 5-10
 * minute, 11-15 hour, 16-20 day, 21-24 month, 25-31 year-since-1980.
 */
static int64_t exfat_time(uint32_t ts) {
    if (ts == 0) return 0;
    int sec  = (int)(ts & 0x1F) * 2;
    int min  = (int)((ts >> 5) & 0x3F);
    int hour = (int)((ts >> 11) & 0x1F);
    int day  = (int)((ts >> 16) & 0x1F);
    int mon  = (int)((ts >> 21) & 0x0F);
    int year = 1980 + (int)((ts >> 25) & 0x7F);
    if (mon < 1 || mon > 12 || day < 1 || day > 31) return 0;

    static const int mdays[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    int64_t days = 0;
    for (int y = 1970; y < year; y++)
        days += ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 366 : 365;
    for (int m = 0; m < mon - 1; m++) {
        days += mdays[m];
        if (m == 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
            days += 1;
    }
    days += day - 1;
    return days * 86400 + hour * 3600 + min * 60 + sec;
}

/* Inverse of exfat_time: seconds-since-epoch (UTC) -> packed exFAT timestamp. */
static uint32_t exfat_encode_time(int64_t t) {
    if (t <= 0) return 0;
    int64_t days = t / 86400;
    int64_t rem  = t % 86400;
    int hour = (int)(rem / 3600); rem %= 3600;
    int min  = (int)(rem / 60);
    int sec  = (int)(rem % 60);

    int year = 1970;
    for (;;) {
        int leap = ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0);
        int64_t dy = leap ? 366 : 365;
        if (days >= dy) { days -= dy; year++; } else break;
    }
    static const int mdays[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    int mon = 0;
    for (; mon < 12; mon++) {
        int dm = mdays[mon];
        if (mon == 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) dm = 29;
        if (days >= dm) days -= dm; else break;
    }
    if (year < 1980) return 0;
    uint32_t ts = 0;
    ts |= (uint32_t)(sec / 2) & 0x1F;
    ts |= ((uint32_t)min  & 0x3F) << 5;
    ts |= ((uint32_t)hour & 0x1F) << 11;
    ts |= ((uint32_t)(days + 1) & 0x1F) << 16;
    ts |= ((uint32_t)(mon + 1)  & 0x0F) << 21;
    ts |= ((uint32_t)(year - 1980) & 0x7F) << 25;
    return ts;
}

static uint32_t exfat_default_mask(uint16_t attr) {
    uint32_t m = (attr & EXFAT_ATTR_DIRECTORY) ? 0755U : 0644U;
    if (attr & EXFAT_ATTR_READ_ONLY) m &= ~0222U;
    return m;
}

/* -------------------------------------------------------------- node alloc */

static fs_node_t *exfat_alloc_node(exfat_fs_t *fs, const char *name, uint64_t inode,
                                   uint32_t first_cluster, uint64_t size,
                                   uint16_t attr, uint8_t no_fat_chain,
                                   int64_t crt, int64_t mod, int64_t acc,
                                   uint8_t has_dir_entry, uint32_t dir_cluster,
                                   uint8_t dir_no_fat_chain, uint64_t primary_index,
                                   uint8_t secondary_count) {
    /*
     * exFAT-F3 / audit H1+H2+L1: the node cache is a fixed slot ring shared by
     * all exFAT mounts.  The old code hardcoded the root to slot 0 (shared
     * across every mount, so a second mount aliased then freed the first
     * mount's root -> cross-volume writes + use-after-free) and populated the
     * chosen slot AFTER dropping the cache lock (a torn node whose ->impl and
     * op-pointers could come from two racing callers).  Mirroring the ext2
     * driver: the root is keyed by (fs, EXFAT_ROOT_INO) like any other node and
     * pinned by exfat_mount(), so different mounts never collide; slot
     * selection AND the full population run as one step under the cache lock.
     */
    exfat_node_t *ctx = NULL;
    fs_node_t *node = NULL;

    mutex_lock(&exfat_node_cache_lock);

    /* Reuse the slot already describing this (fs, inode), if any. */
    for (uint32_t i = 0; i < EXFAT_NODE_CACHE_SIZE; i++) {
        if (exfat_node_cache[i].fs == fs &&
            exfat_fs_node_cache[i].inode == inode &&
            exfat_fs_node_cache[i].name[0] != '\0') {
            ctx = &exfat_node_cache[i];
            node = &exfat_fs_node_cache[i];
            break;
        }
    }
    /* Else take the next UNPINNED slot, round-robin over the whole ring. */
    if (!ctx) {
        uint32_t start = exfat_node_cache_idx;
        for (uint32_t n = 0; n < EXFAT_NODE_CACHE_SIZE; n++) {
            uint32_t i = (start + n) % EXFAT_NODE_CACHE_SIZE;
            if (exfat_node_cache[i].pin == 0) {
                exfat_node_cache_idx = (i + 1) % EXFAT_NODE_CACHE_SIZE;
                ctx = &exfat_node_cache[i];
                node = &exfat_fs_node_cache[i];
                break;
            }
        }
    }
    if (!ctx) {
        mutex_unlock(&exfat_node_cache_lock);
        kprintf("exfat: node cache exhausted (all %d slots pinned)\n",
                EXFAT_NODE_CACHE_SIZE);
        return NULL;
    }

    uint32_t keep_pin = ctx->pin;
    memset(ctx, 0, sizeof(*ctx));
    memset(node, 0, sizeof(*node));
    ctx->pin = keep_pin;   /* re-populating a slot must not drop its pins */

    ctx->fs = fs;
    ctx->first_cluster = first_cluster;
    ctx->size = size;
    ctx->attr = attr;
    ctx->no_fat_chain = no_fat_chain;
    ctx->has_dir_entry = has_dir_entry;
    ctx->dir_cluster = dir_cluster;
    ctx->dir_no_fat_chain = dir_no_fat_chain;
    ctx->primary_index = primary_index;
    ctx->secondary_count = secondary_count;

    strlcpy(node->name, name, sizeof(node->name));
    node->impl = (uintptr_t)ctx;
    node->inode = inode;
    node->length = (off_t)size;
    node->mask = exfat_default_mask(attr);
    node->uid = 0;
    node->gid = 0;
    node->atime = acc;
    node->mtime = mod;
    node->ctime = crt;
    node->statfs = exfat_statfs;
    node->open   = exfat_node_open;
    node->close  = exfat_node_close;

    if (attr & EXFAT_ATTR_DIRECTORY) {
        node->flags = FS_DIRECTORY;
        node->readdir = exfat_readdir;
        node->finddir = exfat_finddir;
        node->mkdir = exfat_mkdir;
        node->mknod = exfat_mknod;
        node->unlink = exfat_unlink;
        node->rmdir = exfat_rmdir;
        node->rename = exfat_rename;
    } else {
        node->flags = FS_FILE;
        node->read = exfat_read;
        node->write = exfat_file_write;
        node->truncate = exfat_truncate;
    }
    /* Populated fully under the lock: no torn-node window for a racing caller. */
    mutex_unlock(&exfat_node_cache_lock);
    return node;
}

/* ---------------------------------------------------- directory iteration */

/*
 * Forward-only iterator over the 32-byte entries of a directory's cluster
 * chain.  exfat_dir_entry() returns a pointer to the i-th entry, loading the
 * containing cluster on demand.  Because the scan reads entries in strictly
 * increasing order, advancing by one cluster is the fast path; a larger jump
 * falls back to walking the chain from the start.
 */
struct exfat_dir_iter {
    exfat_fs_t *fs;
    uint32_t start;
    uint8_t  no_fat_chain;
    uint8_t *buf;                 /* cluster_size bytes */
    uint32_t entries_per_cluster;
    uint32_t chain_index;         /* logical cluster index currently in buf */
    uint32_t cur_cluster;         /* cluster number currently in buf */
    int loaded;
    int eof;
    int io_error;                 /* a device read failed (vs a clean EOF) */
};

static int exfat_iter_to(struct exfat_dir_iter *it, uint32_t want_chain) {
    if (it->loaded && it->chain_index == want_chain) return 0;

    uint32_t c;
    if (it->loaded && want_chain == it->chain_index + 1) {
        if (it->no_fat_chain) {
            c = it->cur_cluster + 1;
            if (!exfat_cluster_valid(it->fs, c)) { it->eof = 1; return -1; }
        } else {
            uint32_t nx = exfat_fat_next(it->fs, it->cur_cluster);
            if (nx < EXFAT_FIRST_CLUSTER || nx >= EXFAT_CLUSTER_END) { it->eof = 1; return -1; }
            c = nx;
        }
    } else {
        c = exfat_chain_nth(it->fs, it->start, it->no_fat_chain, want_chain);
        if (c == 0) { it->eof = 1; return -1; }
    }
    /* audit M4: a failed device read is an I/O error, not a clean end of
     * directory — flag it so exfat_scan_dir returns -EIO instead of -ENOENT. */
    if (exfat_read_cluster(it->fs, c, it->buf) != 0) { it->eof = 1; it->io_error = 1; return -1; }
    it->chain_index = want_chain;
    it->cur_cluster = c;
    it->loaded = 1;
    return 0;
}

static const uint8_t *exfat_dir_entry(struct exfat_dir_iter *it, uint64_t entry_index) {
    uint32_t want_chain = (uint32_t)(entry_index / it->entries_per_cluster);
    uint32_t within = (uint32_t)(entry_index % it->entries_per_cluster);
    if (exfat_iter_to(it, want_chain) != 0) return NULL;
    return it->buf + (size_t)within * 32;
}

/* Parsed result of one directory entry set. */
struct exfat_dirinfo {
    char     name[256];
    uint32_t first_cluster;
    uint64_t size;
    uint16_t attr;
    uint8_t  no_fat_chain;
    uint64_t inode;
    int64_t  crt, mod, acc;
    uint64_t dir_entry_index;   /* index of the 0x85 primary entry */
    uint8_t  secondary_count;   /* # of secondary entries in the set */
};

/*
 * Verify a directory entry set's §6.3.3 SetChecksum by streaming its
 * (secondary_count+1) 32-byte entries through the 16-bit rotate-right checksum,
 * skipping the two SetChecksum bytes at offset 2,3 of the primary.  Returns 1
 * on a match.  Reloads the iterator buffer, so the caller must re-fetch any
 * entry pointer it still needs afterwards.
 */
static int exfat_set_checksum_ok(struct exfat_dir_iter *it, uint64_t primary_ei,
                                 uint8_t secondary_count, uint16_t want) {
    uint16_t c = 0;
    uint32_t nent = 1u + secondary_count;
    for (uint32_t k = 0; k < nent; k++) {
        const uint8_t *p = exfat_dir_entry(it, primary_ei + (uint64_t)k);
        if (!p) return 0;   /* set runs past the allocation -> invalid */
        for (uint32_t j = 0; j < 32; j++) {
            if (k == 0 && (j == 2 || j == 3)) continue;
            c = (uint16_t)(((c << 15) | (c >> 1)) + p[j]);
        }
    }
    return c == want;
}

/*
 * Scan directory `dir`.  If `want_name` is non-NULL, return the entry set
 * whose name matches (ASCII case-insensitive); otherwise return the
 * `want_index`-th set.  Returns 0 on success (fills *info), -ENOENT if not
 * found, or another negative errno on error.
 */
static int exfat_scan_dir(exfat_node_t *dir, const char *want_name,
                          uint64_t want_index, struct exfat_dirinfo *info) {
    exfat_fs_t *fs = dir->fs;
    struct exfat_dir_iter it;
    memset(&it, 0, sizeof(it));
    it.fs = fs;
    it.start = dir->first_cluster;
    it.no_fat_chain = dir->no_fat_chain;
    it.entries_per_cluster = fs->cluster_size / 32;
    if (it.entries_per_cluster == 0) return -EIO;
    it.buf = kmalloc(fs->cluster_size);
    if (!it.buf) return -ENOMEM;

    int rc = -ENOENT;
    uint64_t found = 0;

    for (uint64_t ei = 0; ei < EXFAT_MAX_DIR_ENTRIES; ei++) {
        const uint8_t *e = exfat_dir_entry(&it, ei);
        if (!e) break;                              /* end of allocation */
        uint8_t type = e[0];
        if (type == EXFAT_ENTRY_EOD) break;         /* end of directory */
        if (!(type & EXFAT_ENTRY_INUSE)) continue;  /* deleted / unused */
        if (type != EXFAT_ENTRY_FILE) {
            /* audit I1 / §8.2: an unrecognised in-use *critical primary*
             * ((type & 0xE0) == 0x80) renders the directory invalid.  We reject
             * major!=1 volumes, so such a type cannot be a future benign entry;
             * stop rather than misparse past it.  Benign primaries (0xA0-0xBF)
             * and secondaries are skipped as before. */
            if ((type & 0xE0) == 0x80 &&
                type != EXFAT_ENTRY_BITMAP && type != EXFAT_ENTRY_UPCASE &&
                type != EXFAT_ENTRY_LABEL) {
                kprintf("exfat: unrecognised critical primary 0x%02x; "
                        "stopping directory scan\n", type);
                break;
            }
            continue;                               /* only 0x85 starts a set */
        }

        /* Copy primary fields out before the buffer can be reloaded. */
        const exfat_file_entry_t *fe = (const exfat_file_entry_t *)e;
        uint16_t attr = fe->file_attributes;
        uint8_t  secondary_count = fe->secondary_count;
        uint16_t set_checksum = fe->set_checksum;
        int64_t crt = exfat_time(fe->create_time);
        int64_t mod = exfat_time(fe->modify_time);
        int64_t acc = exfat_time(fe->access_time);

        /* audit H4 / §6.3.3: verify the SetChecksum before using any entry in
         * the set.  This both rejects corrupt sets and stops a hostile
         * SecondaryCount from later steering exfat_delete_set across unrelated
         * neighbouring entries.  (Reloads the iterator buffer.) */
        if (!exfat_set_checksum_ok(&it, ei, secondary_count, set_checksum))
            continue;

        /* Secondary #1 must be the stream extension (0xC0). */
        const uint8_t *se = exfat_dir_entry(&it, ei + 1);
        if (!se || se[0] != EXFAT_ENTRY_STREAM) continue;
        const exfat_stream_entry_t *st = (const exfat_stream_entry_t *)se;
        uint8_t  nlen   = st->name_length;
        if (nlen == 0) continue;                    /* audit L5: §7.6 name is 1..255 */
        uint32_t fc     = st->first_cluster;
        uint64_t sz     = st->data_length;
        uint8_t  sflags = st->flags;

        /* Assemble the UTF-16 name from the 0xC1 file-name entries. */
        uint16_t utf16[256];
        int nchars = 0;
        int name_entries = (nlen + 14) / 15;   /* 15 chars per entry */
        for (int k = 0; k < name_entries; k++) {
            const uint8_t *ne = exfat_dir_entry(&it, ei + 2 + (uint64_t)k);
            if (!ne || ne[0] != EXFAT_ENTRY_NAME) break;
            const exfat_name_entry_t *nm = (const exfat_name_entry_t *)ne;
            for (int j = 0; j < 15 && nchars < (int)nlen && nchars < 255; j++)
                utf16[nchars++] = nm->name[j];    /* consumed before next read */
        }
        char utf8[256];
        exfat_utf16_to_utf8((const uint8_t *)utf16, nchars, utf8, sizeof(utf8));

        int match = want_name ? exfat_name_eq_utf16(fs, utf16, nchars, want_name)
                              : (found == want_index);
        if (match) {
            memset(info, 0, sizeof(*info));
            strlcpy(info->name, utf8, sizeof(info->name));
            info->first_cluster = fc;
            info->size = sz;
            info->attr = attr;
            info->no_fat_chain = (sflags & EXFAT_FLAG_NO_FAT_CHAIN) ? 1 : 0;
            /* Stable, per-slot inode: (dir cluster, entry index). */
            info->inode = ((uint64_t)dir->first_cluster << 32) | (uint32_t)(ei + 1);
            info->crt = crt; info->mod = mod; info->acc = acc;
            info->dir_entry_index = ei;
            info->secondary_count = secondary_count;
            rc = 0;
            break;
        }
        found++;
        /* Secondaries are skipped naturally: they lack the 0x85 type, so the
         * outer loop's per-entry advance walks over them. */
    }

    /* audit M4: a read failure mid-scan must not masquerade as "not found",
     * or rmdir/rename would treat an unreadable directory as empty and mkdir
     * would skip its EEXIST check. */
    if (rc == -ENOENT && it.io_error) rc = -EIO;

    kfree(it.buf, fs->cluster_size);
    return rc;
}

/* --------------------------------------------------------------- VFS ops */

static struct dirent *exfat_readdir(fs_node_t *node, uint64_t index) {
    exfat_node_t *ctx = (exfat_node_t *)(uintptr_t)node->impl;
    if (!ctx) return NULL;

    struct exfat_dirinfo info;
    if (exfat_scan_dir(ctx, NULL, index, &info) != 0) return NULL;

    struct dirent *d = &ctx->current_dirent;
    memset(d, 0, sizeof(*d));
    d->d_ino = info.inode;
    d->d_off = 0;    /* index-based; getdents falls back to its +1 counter */
    strlcpy(d->d_name, info.name, sizeof(d->d_name));
    d->d_namlen = (uint8_t)strlen(d->d_name);
    d->d_reclen = (uint16_t)sizeof(struct dirent);
    d->d_type = (info.attr & EXFAT_ATTR_DIRECTORY) ? DT_DIR : DT_REG;
    return d;
}

static fs_node_t *exfat_finddir(fs_node_t *node, char *name) {
    exfat_node_t *ctx = (exfat_node_t *)(uintptr_t)node->impl;
    if (!ctx || !name) return NULL;

    struct exfat_dirinfo info;
    if (exfat_scan_dir(ctx, name, 0, &info) != 0) return NULL;
    return exfat_alloc_node(ctx->fs, info.name, info.inode, info.first_cluster,
                            info.size, info.attr, info.no_fat_chain,
                            info.crt, info.mod, info.acc,
                            1, ctx->first_cluster, ctx->no_fat_chain,
                            info.dir_entry_index, info.secondary_count);
}

static size_t exfat_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    exfat_node_t *ctx = (exfat_node_t *)(uintptr_t)node->impl;
    if (!ctx || !buffer || offset < 0) return 0;
    exfat_fs_t *fs = ctx->fs;

    uint64_t off = (uint64_t)offset;
    if (off >= ctx->size) return 0;
    uint64_t avail = ctx->size - off;
    if ((uint64_t)size > avail) size = (size_t)avail;
    if (size == 0) return 0;
    if (!exfat_cluster_valid(fs, ctx->first_cluster)) return 0;

    uint8_t *cbuf = kmalloc(fs->cluster_size);
    if (!cbuf) return 0;

    size_t done = 0;
    int io_err = 0;
    uint32_t within = (uint32_t)(off % fs->cluster_size);
    /* audit M2: pass the full 64-bit logical cluster index (was truncated to
     * uint32, which wrapped for large offsets to a valid-looking wrong cluster). */
    uint32_t cluster = exfat_chain_nth(fs, ctx->first_cluster, ctx->no_fat_chain,
                                       off / fs->cluster_size);

    /* audit M8: a single read cannot legitimately span more than ClusterCount
     * clusters; the guard stops a cyclic FAT from looping. */
    uint32_t guard = 0;
    while (done < size && cluster != 0 && guard++ < fs->cluster_count) {
        if (exfat_read_cluster(fs, cluster, cbuf) != 0) { io_err = 1; break; }
        uint32_t chunk = fs->cluster_size - within;
        if ((uint64_t)chunk > (uint64_t)(size - done)) chunk = (uint32_t)(size - done);
        memcpy(buffer + done, cbuf + within, chunk);
        done += chunk;
        within = 0;
        if (done < size) {
            if (ctx->no_fat_chain) {
                cluster = exfat_cluster_valid(fs, cluster + 1) ? cluster + 1 : 0;
            } else {
                uint32_t nx = exfat_fat_next(fs, cluster);
                cluster = (nx >= EXFAT_FIRST_CLUSTER && nx < EXFAT_CLUSTER_END) ? nx : 0;
            }
        }
    }
    kfree(cbuf, fs->cluster_size);
    /* audit L2: surface a device error the caller can distinguish from EOF via
     * the VFS-28 negated-errno channel, but only when no bytes were delivered
     * (a partial read returns its count so the error lands on the next call). */
    if (done == 0 && io_err) return (size_t)-EIO;
    return done;
}

/* --------------------------------------------------- directory entry writes */

/* Disk byte offset of the 32-byte entry at `idx` within a directory chain.
 * Returns 0 on failure (the cluster heap never starts at byte 0). */
static uint64_t exfat_entry_offset(exfat_fs_t *fs, uint32_t dir_start,
                                   int no_fat_chain, uint64_t idx) {
    uint32_t epc = fs->cluster_size / 32;
    if (epc == 0) return 0;
    uint32_t ci = (uint32_t)(idx / epc);
    uint32_t within = (uint32_t)(idx % epc);
    uint32_t c = exfat_chain_nth(fs, dir_start, no_fat_chain, ci);
    if (c == 0) return 0;
    return exfat_cluster_sector(fs, c) * fs->bytes_per_sector + (uint64_t)within * 32;
}

/*
 * Extend a directory by one zeroed, FAT-chained cluster.  Returns the new
 * cluster, or 0 on failure.  Only FAT-chained directories are extendable;
 * NoFatChain directories (rare, from other tools) are refused.
 */
static int exfat_update_stream(exfat_node_t *node, uint32_t first_cluster,
                               int no_fat_chain, uint64_t data_length);

/* §9.5: a directory's DataLength may not exceed 256 MiB. */
#define EXFAT_MAX_DIR_BYTES  (256u * 1024u * 1024u)

static uint32_t exfat_dir_extend(exfat_node_t *dir) {
    exfat_fs_t *fs = dir->fs;
    if (dir->no_fat_chain) return 0;                 /* not supported */
    if (!exfat_cluster_valid(fs, dir->first_cluster)) return 0;
    /* audit L8 / §9.5: refuse to grow a directory past the 256 MiB limit. */
    if (dir->has_dir_entry && dir->size + fs->cluster_size > EXFAT_MAX_DIR_BYTES)
        return 0;

    uint32_t nc = exfat_alloc_cluster_zeroed(fs);
    if (nc == 0) return 0;

    /* Link onto the end of the chain. */
    uint32_t last = dir->first_cluster;
    uint32_t guard = 0;
    while (guard++ < fs->cluster_count) {
        uint32_t x = exfat_fat_next(fs, last);
        if (x < EXFAT_FIRST_CLUSTER || x >= EXFAT_CLUSTER_END) break;
        last = x;
    }
    /* audit M6: if either link write fails, free the new cluster and do NOT
     * grow the recorded size -- otherwise DataLength would exceed the real
     * chain by one cluster. */
    if (exfat_fat_set(fs, last, nc) != 0 ||
        exfat_fat_set(fs, nc, EXFAT_CLUSTER_EOF) != 0) {
        exfat_free_cluster(fs, nc);
        return 0;
    }

    /* Grow the directory's own recorded size (subdirs carry a DataLength). */
    dir->size += fs->cluster_size;
    if (dir->has_dir_entry)
        exfat_update_stream(dir, dir->first_cluster, dir->no_fat_chain, dir->size);
    return nc;
}

/*
 * Find `need` consecutive free entry slots (EOD or deleted) in a directory,
 * extending it if necessary.  Writes the starting entry index to *out_index.
 */
static int exfat_dir_find_free(exfat_node_t *dir, uint32_t need, uint64_t *out_index) {
    exfat_fs_t *fs = dir->fs;
    uint32_t epc = fs->cluster_size / 32;
    if (epc == 0) return -EIO;
    uint8_t *buf = kmalloc(fs->cluster_size);
    if (!buf) return -ENOMEM;

    uint64_t run_start = 0;
    uint32_t run = 0;
    uint32_t ci = 0;
    int rc = -ENOSPC;

    for (;;) {
        uint32_t c = exfat_chain_nth(fs, dir->first_cluster, dir->no_fat_chain, ci);
        if (c == 0) {
            c = exfat_dir_extend(dir);
            if (c == 0) { rc = -ENOSPC; break; }
        }
        if (exfat_read_cluster(fs, c, buf) != 0) { rc = -EIO; break; }

        for (uint32_t w = 0; w < epc; w++) {
            uint8_t type = buf[w * 32];
            uint64_t idx = (uint64_t)ci * epc + w;
            int is_free = (type == EXFAT_ENTRY_EOD) || !(type & EXFAT_ENTRY_INUSE);
            if (is_free) {
                if (run == 0) run_start = idx;
                run++;
                if (run >= need) { *out_index = run_start; rc = 0; break; }
            } else {
                run = 0;
            }
        }
        if (rc == 0) break;
        if (++ci > fs->cluster_count) { rc = -EIO; break; }
    }

    kfree(buf, fs->cluster_size);
    return rc;
}

/*
 * Read the whole entry set of `node` (1 + secondary_count entries), patch its
 * stream extension (FirstCluster / DataLength / ValidDataLength / flags) and
 * modify/access timestamps, recompute the SetChecksum, and write it back.
 * No-op for the synthetic root (has_dir_entry == 0).
 */
static int exfat_update_stream(exfat_node_t *node, uint32_t first_cluster,
                               int no_fat_chain, uint64_t data_length) {
    exfat_fs_t *fs = node->fs;
    if (!node->has_dir_entry) return 0;

    uint32_t nent = 1u + node->secondary_count;
    uint32_t nbytes = nent * 32;
    uint8_t *set = kmalloc(nbytes);
    if (!set) return -ENOMEM;

    for (uint32_t i = 0; i < nent; i++) {
        uint64_t off = exfat_entry_offset(fs, node->dir_cluster,
                                          node->dir_no_fat_chain, node->primary_index + i);
        if (off == 0 || exfat_read_bytes(fs, off, 32, set + i * 32) != 0) {
            kfree(set, nbytes);
            return -EIO;
        }
    }

    exfat_file_entry_t   *fe = (exfat_file_entry_t *)set;
    exfat_stream_entry_t *st = (exfat_stream_entry_t *)(set + 32);
    if (fe->entry_type != EXFAT_ENTRY_FILE || st->entry_type != EXFAT_ENTRY_STREAM) {
        kfree(set, nbytes);
        return -EIO;
    }

    st->flags = (uint8_t)((st->flags & ~EXFAT_FLAG_NO_FAT_CHAIN) | EXFAT_FLAG_ALLOC_POSSIBLE);
    if (no_fat_chain) st->flags |= EXFAT_FLAG_NO_FAT_CHAIN;
    st->first_cluster = first_cluster;
    st->valid_data_length = data_length;
    st->data_length = data_length;

    uint32_t now = exfat_encode_time(get_time());
    fe->modify_time = now;
    fe->access_time = now;
    fe->set_checksum = exfat_set_checksum(set, nbytes);

    int rc = 0;
    for (uint32_t i = 0; i < nent; i++) {
        uint64_t off = exfat_entry_offset(fs, node->dir_cluster,
                                          node->dir_no_fat_chain, node->primary_index + i);
        if (off == 0 || exfat_write_bytes(fs, off, 32, set + i * 32) != 0) {
            rc = -EIO;
            break;
        }
    }
    kfree(set, nbytes);
    return rc;
}

/*
 * Build a fresh entry set (0x85 + 0xC0 + 0xC1...) for `name` with the given
 * attributes/cluster/size, find free slots in `dir`, and write it.  Returns
 * the primary entry index and secondary count via out params.
 */
static int exfat_create_set(exfat_node_t *dir, const char *name, uint16_t attr,
                            uint32_t first_cluster, uint64_t size, int no_fat_chain,
                            uint64_t *out_primary, uint8_t *out_secondary) {
    exfat_fs_t *fs = dir->fs;

    uint16_t u16[256];
    int nlen = exfat_utf8_to_utf16(name, u16, 255);
    if (nlen <= 0 || nlen > 255) return -EINVAL;

    int name_entries = (nlen + 14) / 15;
    uint32_t secondary = 1u + (uint32_t)name_entries;   /* stream + name entries */
    uint32_t nent = 1u + secondary;
    uint32_t nbytes = nent * 32;
    uint8_t *set = kmalloc(nbytes);
    if (!set) return -ENOMEM;
    memset(set, 0, nbytes);

    uint32_t now = exfat_encode_time(get_time());

    exfat_file_entry_t *fe = (exfat_file_entry_t *)set;
    fe->entry_type = EXFAT_ENTRY_FILE;
    fe->secondary_count = (uint8_t)secondary;
    fe->set_checksum = 0;
    fe->file_attributes = attr;
    fe->create_time = now;
    fe->modify_time = now;
    fe->access_time = now;

    exfat_stream_entry_t *st = (exfat_stream_entry_t *)(set + 32);
    st->entry_type = EXFAT_ENTRY_STREAM;
    st->flags = (uint8_t)(EXFAT_FLAG_ALLOC_POSSIBLE | (no_fat_chain ? EXFAT_FLAG_NO_FAT_CHAIN : 0));
    st->name_length = (uint8_t)nlen;
    st->name_hash = exfat_name_hash(fs, u16, nlen);
    st->valid_data_length = size;
    st->first_cluster = first_cluster;
    st->data_length = size;

    for (int k = 0; k < name_entries; k++) {
        uint8_t *ne = set + 32 * (2 + k);
        ne[0] = EXFAT_ENTRY_NAME;
        ne[1] = 0;
        for (int j = 0; j < 15; j++) {
            int ci = k * 15 + j;
            uint16_t ch = (ci < nlen) ? u16[ci] : 0;
            ne[2 + j * 2]     = (uint8_t)(ch & 0xFF);
            ne[2 + j * 2 + 1] = (uint8_t)(ch >> 8);
        }
    }

    fe->set_checksum = exfat_set_checksum(set, nbytes);

    uint64_t idx = 0;
    int rc = exfat_dir_find_free(dir, nent, &idx);
    if (rc != 0) { kfree(set, nbytes); return rc; }

    for (uint32_t i = 0; i < nent; i++) {
        uint64_t off = exfat_entry_offset(fs, dir->first_cluster, dir->no_fat_chain, idx + i);
        if (off == 0 || exfat_write_bytes(fs, off, 32, set + i * 32) != 0) {
            /* audit M6: a device error partway through leaves a torn set that
             * (lacking a valid SetChecksum) scan_dir already rejects -- but
             * clear the InUse bit on the entries we did write so it also stops
             * enumerating and never confuses another implementation. */
            for (uint32_t j = 0; j < i; j++) {
                uint64_t joff = exfat_entry_offset(fs, dir->first_cluster,
                                                   dir->no_fat_chain, idx + j);
                if (joff == 0) break;
                uint8_t t;
                if (exfat_read_bytes(fs, joff, 1, &t) != 0) break;
                t &= (uint8_t)~EXFAT_ENTRY_INUSE;
                exfat_write_bytes(fs, joff, 1, &t);
            }
            kfree(set, nbytes);
            return -EIO;
        }
    }
    kfree(set, nbytes);
    if (out_primary)   *out_primary = idx;
    if (out_secondary) *out_secondary = (uint8_t)secondary;
    return 0;
}

/* Delete an entry set: clear the "in use" bit on each of its entries.
 * audit H4: the primary is cleared first (so the set stops enumerating even if
 * a later write fails), and each entry's type is checked against its expected
 * category before clearing -- entry 0 an in-use critical primary, the rest
 * in-use secondaries -- so a bogus secondary_count can never steer this across
 * unrelated neighbouring entries.  (scan_dir already validates the SetChecksum,
 * making secondary_count trustworthy; this is defence in depth.) */
static int exfat_delete_set(exfat_node_t *dir, uint64_t primary_index,
                            uint8_t secondary_count) {
    exfat_fs_t *fs = dir->fs;
    uint32_t nent = 1u + secondary_count;
    for (uint32_t i = 0; i < nent; i++) {
        uint64_t off = exfat_entry_offset(fs, dir->first_cluster, dir->no_fat_chain,
                                          primary_index + i);
        if (off == 0) return -EIO;
        uint8_t type;
        if (exfat_read_bytes(fs, off, 1, &type) != 0) return -EIO;
        /* (type & 0xC0): 0x80 = in-use primary, 0xC0 = in-use secondary. */
        uint8_t expect = (i == 0) ? 0x80 : 0xC0;
        if ((type & 0xC0) != expect) break;       /* not part of this set; stop */
        type &= (uint8_t)~EXFAT_ENTRY_INUSE;      /* 0x85->0x05, 0xC0->0x40, 0xC1->0x41 */
        if (exfat_write_bytes(fs, off, 1, &type) != 0) return -EIO;
    }
    return 0;
}

/* ------------------------------------------------------------ file writes */

static size_t exfat_file_write_locked(fs_node_t *node, off_t offset, size_t size,
                                      const uint8_t *buffer) {
    exfat_node_t *ctx = (exfat_node_t *)(uintptr_t)node->impl;
    if (!ctx || !buffer || offset < 0 || size == 0) return 0;
    exfat_fs_t *fs = ctx->fs;
    uint32_t cs = fs->cluster_size;

    uint64_t end = (uint64_t)offset + size;
    /* audit M2: refuse a write whose last byte lands beyond the last possible
     * cluster; this also stops need_clusters / the logical index below from
     * being truncated into a valid-looking wrong cluster. */
    if ((end - 1) / cs >= (uint64_t)fs->cluster_count) return (size_t)-EFBIG;
    uint32_t need_clusters = (uint32_t)((end + cs - 1) / cs);
    uint32_t have_clusters = (uint32_t)((ctx->size + cs - 1) / cs);

    /* audit M6: reject up front if the volume lacks room for the whole
     * extension, rather than allocating some clusters and then failing partway
     * -- which would leave a chain longer than the recorded DataLength. */
    if (need_clusters > have_clusters &&
        (uint64_t)(need_clusters - have_clusters) > fs->free_clusters)
        return (size_t)-ENOSPC;

    /* Extending a contiguous (NoFatChain) file: lay down its FAT chain first so
     * the new clusters can be non-contiguous. */
    if (ctx->no_fat_chain && need_clusters > have_clusters &&
        exfat_cluster_valid(fs, ctx->first_cluster)) {
        for (uint32_t i = 0; i < have_clusters; i++) {
            uint32_t c = ctx->first_cluster + i;
            uint32_t nx = (i + 1 < have_clusters) ? (c + 1) : EXFAT_CLUSTER_EOF;
            exfat_fat_set(fs, c, nx);
        }
        ctx->no_fat_chain = 0;
    }

    /* Allocate the first cluster if the file is empty (zeroed — audit H7). */
    if (!exfat_cluster_valid(fs, ctx->first_cluster)) {
        uint32_t nc = exfat_alloc_cluster_zeroed(fs);
        if (nc == 0) return 0;
        ctx->first_cluster = nc;
        ctx->no_fat_chain = 0;
    }

    /* Walk to the need_clusters-th cluster, extending the FAT chain as needed.
     * New clusters are zeroed (audit H7) so a hole or unwritten tail reads back
     * as zeroes instead of a previously-deleted file's residue. */
    uint32_t cluster = ctx->first_cluster;
    for (uint32_t i = 1; i < need_clusters; i++) {
        uint32_t nx;
        if (ctx->no_fat_chain) {
            nx = cluster + 1;                    /* contiguous, already allocated */
        } else {
            nx = exfat_fat_next(fs, cluster);
            if (nx < EXFAT_FIRST_CLUSTER || nx >= EXFAT_CLUSTER_END) {
                nx = exfat_alloc_cluster_zeroed(fs);
                if (nx == 0) break;
                exfat_fat_set(fs, cluster, nx);
                exfat_fat_set(fs, nx, EXFAT_CLUSTER_EOF);
            }
        }
        cluster = nx;
    }

    uint8_t *cbuf = kmalloc(cs);
    if (!cbuf) return 0;

    size_t done = 0;
    uint32_t within = (uint32_t)((uint64_t)offset % cs);
    uint32_t c = exfat_chain_nth(fs, ctx->first_cluster, ctx->no_fat_chain,
                                 (uint64_t)offset / cs);   /* audit M2: 64-bit index */
    while (done < size && c != 0) {
        uint32_t chunk = cs - within;
        if ((uint64_t)chunk > (uint64_t)(size - done)) chunk = (uint32_t)(size - done);
        if (within != 0 || chunk < cs) {
            if (exfat_read_cluster(fs, c, cbuf) != 0) break;   /* RMW partial cluster */
        }
        memcpy(cbuf + within, buffer + done, chunk);
        if (exfat_write_cluster(fs, c, cbuf) != 0) break;
        done += chunk;
        within = 0;
        if (done < size) {
            if (ctx->no_fat_chain) {
                c = exfat_cluster_valid(fs, c + 1) ? c + 1 : 0;
            } else {
                uint32_t nx = exfat_fat_next(fs, c);
                c = (nx >= EXFAT_FIRST_CLUSTER && nx < EXFAT_CLUSTER_END) ? nx : 0;
            }
        }
    }
    kfree(cbuf, cs);

    if ((uint64_t)offset + done > ctx->size) {
        ctx->size = (uint64_t)offset + done;
        node->length = (off_t)ctx->size;
    }
    /* audit H6: honour exfat_update_stream's return instead of ignoring it.
     * Only fail the call when nothing was written -- a positive count means the
     * data reached its (unchanged first_cluster) clusters even if the metadata
     * re-stamp failed; the stale-node-after-rename case that made this fail is
     * fixed at the source (rename now updates the cached node's entry location). */
    int urc = exfat_update_stream(ctx, ctx->first_cluster, ctx->no_fat_chain, ctx->size);
    if (done == 0 && urc != 0) return (size_t)-EIO;
    return done;
}

static int exfat_truncate_locked(fs_node_t *node, off_t new_size) {
    exfat_node_t *ctx = (exfat_node_t *)(uintptr_t)node->impl;
    if (!ctx || new_size < 0) return -EINVAL;
    exfat_fs_t *fs = ctx->fs;
    uint32_t cs = fs->cluster_size;
    uint64_t ns = (uint64_t)new_size;
    /* audit M2: cap the new size at the volume's cluster capacity so `need`
     * cannot be truncated to a bogus (small) cluster count. */
    if (ns && (ns - 1) / cs >= (uint64_t)fs->cluster_count) return -EFBIG;
    uint32_t need = (uint32_t)((ns + cs - 1) / cs);
    uint32_t have = (uint32_t)((ctx->size + cs - 1) / cs);

    if (ns == 0) {
        if (exfat_cluster_valid(fs, ctx->first_cluster))
            exfat_free_chain(fs, ctx->first_cluster, ctx->no_fat_chain, ctx->size);
        ctx->first_cluster = 0;
        ctx->no_fat_chain = 0;
        ctx->size = 0;
        node->length = 0;
        return exfat_update_stream(ctx, 0, 0, 0);
    }

    if (need < have) {
        /* Shrink: free the clusters past the new end. */
        if (ctx->no_fat_chain) {
            for (uint32_t i = need; i < have; i++)
                exfat_free_cluster(fs, ctx->first_cluster + i);
        } else {
            uint32_t cluster = ctx->first_cluster;
            for (uint32_t i = 1; i < need; i++) {
                uint32_t nx = exfat_fat_next(fs, cluster);
                if (nx < EXFAT_FIRST_CLUSTER || nx >= EXFAT_CLUSTER_END) break;
                cluster = nx;
            }
            uint32_t rest = exfat_fat_next(fs, cluster);
            exfat_fat_set(fs, cluster, EXFAT_CLUSTER_EOF);
            if (rest >= EXFAT_FIRST_CLUSTER && rest < EXFAT_CLUSTER_END)
                exfat_free_chain(fs, rest, 0, (uint64_t)(have - need) * cs);
        }
    } else if (need > have) {
        /* Grow: allocate the extra clusters so DataLength stays backed.
         * audit M6: check free space up front so we never allocate part of the
         * extension and then fail, leaving a chain longer than DataLength that a
         * retried grow would sever and orphan. */
        if ((uint64_t)(need - have) > fs->free_clusters) return -ENOSPC;
        if (ctx->no_fat_chain && exfat_cluster_valid(fs, ctx->first_cluster)) {
            for (uint32_t i = 0; i < have; i++) {
                uint32_t cc = ctx->first_cluster + i;
                exfat_fat_set(fs, cc, (i + 1 < have) ? (cc + 1) : EXFAT_CLUSTER_EOF);
            }
            ctx->no_fat_chain = 0;
        }
        if (!exfat_cluster_valid(fs, ctx->first_cluster)) {
            uint32_t nc = exfat_alloc_cluster_zeroed(fs);   /* audit H7 */
            if (nc == 0) return -ENOSPC;
            ctx->first_cluster = nc;
            ctx->no_fat_chain = 0;
            have = 1;
        }
        uint32_t cluster = ctx->first_cluster;
        for (uint32_t i = 1; i < have; i++) {
            uint32_t nx = exfat_fat_next(fs, cluster);
            if (nx < EXFAT_FIRST_CLUSTER || nx >= EXFAT_CLUSTER_END) break;
            cluster = nx;
        }
        for (uint32_t i = have; i < need; i++) {
            uint32_t nc = exfat_alloc_cluster_zeroed(fs);   /* audit H7 */
            if (nc == 0) return -ENOSPC;
            exfat_fat_set(fs, cluster, nc);
            exfat_fat_set(fs, nc, EXFAT_CLUSTER_EOF);
            cluster = nc;
        }
    }

    ctx->size = ns;
    node->length = new_size;
    return exfat_update_stream(ctx, ctx->first_cluster, ctx->no_fat_chain, ctx->size);
}

/* ----------------------------------------------------- create / delete ops */

static int exfat_mkdir_locked(fs_node_t *parent, const char *name, uint16_t perm) {
    (void)perm;
    exfat_node_t *dir = (exfat_node_t *)(uintptr_t)parent->impl;
    if (!dir || !name || !name[0]) return -EINVAL;
    if ((parent->flags & 0x7) != FS_DIRECTORY) return -ENOTDIR;
    if (!strcmp(name, ".") || !strcmp(name, "..")) return -EINVAL;
    exfat_fs_t *fs = dir->fs;

    struct exfat_dirinfo info;
    int erc = exfat_scan_dir(dir, name, 0, &info);
    if (erc == 0) return -EEXIST;
    if (erc != -ENOENT) return erc;             /* audit M4: don't create over an unreadable dir */

    /* A new (empty) directory occupies one zeroed cluster (no "." / ".."). */
    uint32_t c = exfat_alloc_cluster(fs);
    if (c == 0) return -ENOSPC;
    uint8_t *z = kmalloc(fs->cluster_size);
    if (!z) { exfat_free_cluster(fs, c); return -ENOMEM; }
    memset(z, 0, fs->cluster_size);
    if (exfat_write_cluster(fs, c, z) != 0) {
        kfree(z, fs->cluster_size);
        exfat_free_cluster(fs, c);
        return -EIO;
    }
    kfree(z, fs->cluster_size);

    uint64_t pidx; uint8_t sec;
    int rc = exfat_create_set(dir, name, EXFAT_ATTR_DIRECTORY, c,
                              fs->cluster_size, 0, &pidx, &sec);
    if (rc != 0) { exfat_free_cluster(fs, c); return rc; }
    return 0;
}

static int exfat_mknod_locked(fs_node_t *parent, const char *name, uint16_t mode, uint32_t dev) {
    (void)dev;
    exfat_node_t *dir = (exfat_node_t *)(uintptr_t)parent->impl;
    if (!dir || !name || !name[0]) return -EINVAL;
    if ((parent->flags & 0x7) != FS_DIRECTORY) return -ENOTDIR;
    if (!strcmp(name, ".") || !strcmp(name, "..")) return -EINVAL;

    uint16_t type = mode & S_IFMT;
    if (type == 0) type = S_IFREG;
    if (type == S_IFDIR) return -EISDIR;
    if (type != S_IFREG) return -EPERM;   /* exFAT has no device/fifo/socket nodes */

    struct exfat_dirinfo info;
    int erc = exfat_scan_dir(dir, name, 0, &info);
    if (erc == 0) return -EEXIST;
    if (erc != -ENOENT) return erc;             /* audit M4 */

    uint64_t pidx; uint8_t sec;
    return exfat_create_set(dir, name, EXFAT_ATTR_ARCHIVE, 0, 0, 0, &pidx, &sec);
}

static int exfat_unlink_locked(fs_node_t *parent, const char *name) {
    exfat_node_t *dir = (exfat_node_t *)(uintptr_t)parent->impl;
    if (!dir || !name) return -EINVAL;
    exfat_fs_t *fs = dir->fs;

    struct exfat_dirinfo info;
    int rc = exfat_scan_dir(dir, name, 0, &info);
    if (rc != 0) return rc;
    if (info.attr & EXFAT_ATTR_DIRECTORY) return -EISDIR;

    /* audit M7 / §8.1: remove the directory entry BEFORE freeing the chain, so
     * an interrupted delete leaks clusters rather than leaving a live entry
     * pointing at clusters the allocator can reuse (a cross-link). */
    rc = exfat_delete_set(dir, info.dir_entry_index, info.secondary_count);
    if (rc != 0) return rc;
    if (exfat_cluster_valid(fs, info.first_cluster)) {
        /* audit H6: if the file is still open, defer freeing its chain until the
         * last fd closes -- otherwise the freed clusters get reused and the open
         * fd scribbles another file's data. */
        uint64_t ino = ((uint64_t)dir->first_cluster << 32) |
                       (uint32_t)(info.dir_entry_index + 1);
        if (!exfat_defer_or_free(fs, ino, info.first_cluster, info.no_fat_chain, info.size))
            exfat_free_chain(fs, info.first_cluster, info.no_fat_chain, info.size);
    }
    return 0;
}

/*
 * audit L7 / §8.2: when deleting a directory, free the cluster allocations of
 * any unrecognised *benign primary* entries it holds (in-use, (type&0xE0)==0xA0,
 * with GeneralPrimaryFlags.AllocationPossible set).  The emptiness check only
 * looks for 0x85 File entries, so without this those allocations leak.  This
 * driver never creates such entries; it matters only for foreign-authored
 * volumes.
 */
static void exfat_free_benign_allocs(exfat_fs_t *fs, uint32_t dir_first, int dir_nfc) {
    struct exfat_dir_iter it;
    memset(&it, 0, sizeof(it));
    it.fs = fs;
    it.start = dir_first;
    it.no_fat_chain = (uint8_t)dir_nfc;
    it.entries_per_cluster = fs->cluster_size / 32;
    if (it.entries_per_cluster == 0) return;
    it.buf = kmalloc(fs->cluster_size);
    if (!it.buf) return;
    for (uint64_t ei = 0; ei < EXFAT_MAX_DIR_ENTRIES; ei++) {
        const uint8_t *e = exfat_dir_entry(&it, ei);
        if (!e) break;
        uint8_t type = e[0];
        if (type == EXFAT_ENTRY_EOD) break;
        if (!(type & EXFAT_ENTRY_INUSE)) continue;
        if ((type & 0xE0) != 0xA0) continue;            /* benign primary only */
        uint8_t gpflags = e[4];                          /* GeneralPrimaryFlags */
        if (!(gpflags & EXFAT_FLAG_ALLOC_POSSIBLE)) continue;
        uint32_t fc = exfat_le32(e + 20);
        uint64_t dl = exfat_le64(e + 24);
        int nfc = (gpflags & EXFAT_FLAG_NO_FAT_CHAIN) ? 1 : 0;
        if (exfat_cluster_valid(fs, fc))
            exfat_free_chain(fs, fc, nfc, dl);
    }
    kfree(it.buf, fs->cluster_size);
}

static int exfat_rmdir_locked(fs_node_t *parent, const char *name) {
    exfat_node_t *dir = (exfat_node_t *)(uintptr_t)parent->impl;
    if (!dir || !name) return -EINVAL;
    exfat_fs_t *fs = dir->fs;

    struct exfat_dirinfo info;
    int rc = exfat_scan_dir(dir, name, 0, &info);
    if (rc != 0) return rc;
    if (!(info.attr & EXFAT_ATTR_DIRECTORY)) return -ENOTDIR;

    /* Reject a non-empty directory (any live 0x85 entry inside). */
    exfat_node_t tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.fs = fs;
    tmp.first_cluster = info.first_cluster;
    tmp.no_fat_chain = info.no_fat_chain;
    struct exfat_dirinfo child;
    int crc = exfat_scan_dir(&tmp, NULL, 0, &child);
    if (crc == 0) return -ENOTEMPTY;
    if (crc != -ENOENT) return crc;             /* audit M4: unreadable != empty */

    /* audit M7 / §8.1: delete the entry before freeing the chain. */
    rc = exfat_delete_set(dir, info.dir_entry_index, info.secondary_count);
    if (rc != 0) return rc;
    if (exfat_cluster_valid(fs, info.first_cluster)) {
        /* audit L7: reclaim any benign-primary allocations before the chain. */
        exfat_free_benign_allocs(fs, info.first_cluster, info.no_fat_chain);
        exfat_free_chain(fs, info.first_cluster, info.no_fat_chain, info.size);
    }
    return 0;
}

/*
 * audit M5: does directory `target_fc` equal, or lie anywhere inside, the
 * subtree rooted at `ancestor_fc`?  exFAT records no ".."/parent links, so the
 * only way to detect "moving a directory into its own subtree" is to walk down.
 * Bounded by a depth cap; on too-deep nesting (or a pre-existing cycle) it
 * conservatively returns 1 (refuse the rename) rather than risk a loop.
 */
static int exfat_dir_contains(exfat_fs_t *fs, uint32_t ancestor_fc, int ancestor_nfc,
                              uint32_t target_fc, int depth) {
    if (ancestor_fc == target_fc) return 1;
    if (depth > 64) return 1;
    exfat_node_t tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.fs = fs;
    tmp.first_cluster = ancestor_fc;
    tmp.no_fat_chain = (uint8_t)ancestor_nfc;
    for (uint64_t idx = 0; ; idx++) {
        struct exfat_dirinfo di;
        if (exfat_scan_dir(&tmp, NULL, idx, &di) != 0) break;   /* end or error */
        if ((di.attr & EXFAT_ATTR_DIRECTORY) &&
            exfat_dir_contains(fs, di.first_cluster, di.no_fat_chain, target_fc, depth + 1))
            return 1;
    }
    return 0;
}

static int exfat_rename_locked(fs_node_t *old_parent, const char *old_name,
                               fs_node_t *new_parent, const char *new_name) {
    exfat_node_t *odir = (exfat_node_t *)(uintptr_t)old_parent->impl;
    exfat_node_t *ndir = (exfat_node_t *)(uintptr_t)new_parent->impl;
    if (!odir || !ndir || !old_name || !new_name) return -EINVAL;
    /* audit M12/M5: reserved names must never be recorded. */
    if (!strcmp(new_name, ".") || !strcmp(new_name, "..")) return -EINVAL;
    exfat_fs_t *fs = odir->fs;

    struct exfat_dirinfo src;
    int rc = exfat_scan_dir(odir, old_name, 0, &src);
    if (rc != 0) return rc;
    uint64_t src_inode = ((uint64_t)odir->first_cluster << 32) |
                         (uint32_t)(src.dir_entry_index + 1);

    /* audit M5: refuse to move a directory into itself or its own subtree,
     * which would orphan it into a disconnected cycle. */
    if ((src.attr & EXFAT_ATTR_DIRECTORY) &&
        odir->first_cluster != ndir->first_cluster &&
        exfat_dir_contains(fs, src.first_cluster, src.no_fat_chain, ndir->first_cluster, 0))
        return -EINVAL;

    /* Resolve the destination.  audit H5: if it resolves to the SAME entry set
     * as the source (a case-only or no-op rename, matched up-case-folded), it is
     * NOT an existing file to remove -- the old code freed the source's own
     * clusters here.  Skip removal in that case. */
    struct exfat_dirinfo dst;
    int drc = exfat_scan_dir(ndir, new_name, 0, &dst);
    if (drc != 0 && drc != -ENOENT) return drc;             /* audit M4 */
    int have_dst = (drc == 0);
    int same_entry = have_dst &&
                     odir->first_cluster == ndir->first_cluster &&
                     dst.dir_entry_index == src.dir_entry_index;

    if (have_dst && !same_entry) {
        if (dst.attr & EXFAT_ATTR_DIRECTORY) {
            exfat_node_t tmp;
            memset(&tmp, 0, sizeof(tmp));
            tmp.fs = fs;
            tmp.first_cluster = dst.first_cluster;
            tmp.no_fat_chain = dst.no_fat_chain;
            struct exfat_dirinfo dchild;
            int cc = exfat_scan_dir(&tmp, NULL, 0, &dchild);
            if (cc == 0) return -ENOTEMPTY;
            if (cc != -ENOENT) return cc;                   /* audit M4 */
        }
        /* §8.1 order: delete the entry, then free the chain (deferred if open). */
        int derc = exfat_delete_set(ndir, dst.dir_entry_index, dst.secondary_count);
        if (derc != 0) return derc;
        if (exfat_cluster_valid(fs, dst.first_cluster)) {
            uint64_t dino = ((uint64_t)ndir->first_cluster << 32) |
                            (uint32_t)(dst.dir_entry_index + 1);
            if (!exfat_defer_or_free(fs, dino, dst.first_cluster, dst.no_fat_chain, dst.size))
                exfat_free_chain(fs, dst.first_cluster, dst.no_fat_chain, dst.size);
        }
    }

    /* Write a new set in the target directory pointing at the SAME clusters.
     * The source set is still live, so create_set cannot overwrite it. */
    uint64_t pidx; uint8_t sec;
    rc = exfat_create_set(ndir, new_name, src.attr, src.first_cluster,
                          src.size, src.no_fat_chain, &pidx, &sec);
    if (rc != 0) return rc;

    /* Remove the old set WITHOUT freeing the (reused) cluster chain. */
    rc = exfat_delete_set(odir, src.dir_entry_index, src.secondary_count);
    if (rc != 0) return rc;

    /* audit H6: point any open cached node at the new entry location so a stale
     * fd keeps updating the right directory entry. */
    uint64_t new_inode = ((uint64_t)ndir->first_cluster << 32) | (uint32_t)(pidx + 1);
    exfat_relocate_cached(fs, src_inode, ndir->first_cluster, ndir->no_fat_chain,
                          pidx, sec, new_inode);
    return 0;
}

/*
 * audit M7 / §3.1.13.2: set or clear the VolumeDirty bit (bit 1 of VolumeFlags,
 * boot byte 106).  We set it once at mount (marking the read-write session) and
 * clear it at unmount, so an unclean shutdown leaves it set for the next mount
 * to warn about.  VolumeFlags is excluded from the boot checksum, so no
 * recompute is needed.
 */
static void exfat_set_volume_dirty(exfat_fs_t *fs, int dirty) {
    uint16_t vf = 0;
    if (exfat_read_bytes(fs, offsetof(exfat_boot_t, volume_flags), 2, &vf) != 0) return;
    if (dirty) vf |= EXFAT_VOLFLAG_DIRTY;
    else       vf &= (uint16_t)~EXFAT_VOLFLAG_DIRTY;
    exfat_write_bytes(fs, offsetof(exfat_boot_t, volume_flags), 2, &vf);
}

/* audit L9 / §3.1.18: we do not track PercentInUse precisely, so mark it "not
 * available" (0xFF, boot byte 112, also checksum-exempt) instead of leaving a
 * stale figure for other implementations. */
static void exfat_set_percent_unknown(exfat_fs_t *fs) {
    uint8_t pct = 0xFF;
    exfat_write_bytes(fs, offsetof(exfat_boot_t, percent_in_use), 1, &pct);
}

static int exfat_statfs(fs_node_t *node, struct statfs *buf) {
    if (!node || !buf) return -EINVAL;
    exfat_node_t *ctx = (exfat_node_t *)(uintptr_t)node->impl;
    if (!ctx || !ctx->fs) return -EINVAL;
    exfat_fs_t *fs = ctx->fs;

    memset(buf, 0, sizeof(*buf));
    buf->f_bsize  = fs->cluster_size;
    buf->f_iosize = fs->cluster_size;
    buf->f_blocks = fs->cluster_count;
    /* Free-cluster count comes from the in-memory allocation bitmap. */
    buf->f_bfree  = fs->free_clusters;
    buf->f_bavail = fs->free_clusters;
    buf->f_files  = 0;
    buf->f_ffree  = 0;
    strlcpy(buf->f_fstypename, "exfat", sizeof(buf->f_fstypename));
    return 0;
}

static int exfat_unmount(fs_node_t *root) {
    if (!root) return -EINVAL;
    exfat_node_t *ctx = (exfat_node_t *)(uintptr_t)root->impl;
    if (!ctx || !ctx->fs) return -EINVAL;
    exfat_fs_t *fs = ctx->fs;

    /* audit M1: exclude any in-flight mutation (which holds fs->lock) before
     * tearing the mount down, and clear this mount's node-cache slots under the
     * cache lock the allocator/open/close use -- the old teardown raced them. */
    mutex_lock(&fs->lock);
    mutex_lock(&exfat_node_cache_lock);
    for (uint32_t i = 0; i < EXFAT_NODE_CACHE_SIZE; i++) {
        if (exfat_node_cache[i].fs == fs) {
            memset(&exfat_node_cache[i], 0, sizeof(exfat_node_t));
            memset(&exfat_fs_node_cache[i], 0, sizeof(fs_node_t));
        }
    }
    mutex_unlock(&exfat_node_cache_lock);
    mutex_unlock(&fs->lock);

    /* audit M7: a clean unmount clears VolumeDirty (device still valid here). */
    exfat_set_volume_dirty(fs, 0);

    if (fs->bitmap) kfree(fs->bitmap, fs->bitmap_bytes);
    if (fs->upcase) kfree(fs->upcase, 65536 * sizeof(uint16_t));
    kfree(fs, sizeof(exfat_fs_t));
    return 0;
}

/* Read a byte stream following a cluster chain into `dst` (nbytes). */
static int exfat_read_stream(exfat_fs_t *fs, uint32_t first, int no_fat_chain,
                             uint64_t nbytes, uint8_t *dst) {
    uint8_t *cbuf = kmalloc(fs->cluster_size);
    if (!cbuf) return -ENOMEM;
    uint64_t done = 0;
    uint32_t ci = 0;
    while (done < nbytes) {
        uint32_t c = exfat_chain_nth(fs, first, no_fat_chain, ci);
        if (c == 0 || exfat_read_cluster(fs, c, cbuf) != 0) {
            kfree(cbuf, fs->cluster_size);
            return -EIO;
        }
        uint32_t chunk = fs->cluster_size;
        if ((uint64_t)chunk > nbytes - done) chunk = (uint32_t)(nbytes - done);
        memcpy(dst + done, cbuf, chunk);
        done += chunk;
        ci++;
    }
    kfree(cbuf, fs->cluster_size);
    return 0;
}

static uint32_t exfat_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t exfat_le64(const uint8_t *p) {
    return (uint64_t)exfat_le32(p) | ((uint64_t)exfat_le32(p + 4) << 32);
}

/*
 * Mount-time metadata: locate the allocation-bitmap (0x81) and up-case-table
 * (0x82) entries in the root directory, load the bitmap into memory, and load
 * + decompress the up-case table into a full 65536-entry BMP fold array.
 */
static int exfat_load_metadata(exfat_fs_t *fs) {
    uint8_t *cbuf = kmalloc(fs->cluster_size);
    if (!cbuf) return -ENOMEM;

    uint32_t bitmap_cluster = 0, upcase_cluster = 0;
    uint8_t  bitmap_flags = 0, upcase_flags = 0;
    uint64_t bitmap_len = 0, upcase_len = 0;
    int found_bitmap = 0, found_upcase = 0, done = 0;

    uint32_t cluster = fs->root_cluster;
    uint32_t guard = 0;
    while (!done && exfat_cluster_valid(fs, cluster) && guard++ < fs->cluster_count) {
        if (exfat_read_cluster(fs, cluster, cbuf) != 0) break;
        for (uint32_t off = 0; off + 32 <= fs->cluster_size; off += 32) {
            uint8_t type = cbuf[off];
            if (type == EXFAT_ENTRY_EOD) { done = 1; break; }
            if (type == EXFAT_ENTRY_BITMAP) {
                bitmap_flags   = cbuf[off + 1];
                bitmap_cluster = exfat_le32(cbuf + off + 20);
                bitmap_len     = exfat_le64(cbuf + off + 24);
                found_bitmap = 1;
            } else if (type == EXFAT_ENTRY_UPCASE) {
                upcase_flags   = cbuf[off + 1];
                upcase_cluster = exfat_le32(cbuf + off + 20);
                upcase_len     = exfat_le64(cbuf + off + 24);
                found_upcase = 1;
            }
        }
        if (done) break;
        uint32_t nx = exfat_fat_next(fs, cluster);
        if (nx < EXFAT_FIRST_CLUSTER || nx >= EXFAT_CLUSTER_END) break;
        cluster = nx;
    }
    kfree(cbuf, fs->cluster_size);

    if (!found_bitmap || !found_upcase) return -1;
    /* audit L4: §7.1.5 needs one bit per cluster, so the bitmap DataLength must
     * be at least ceil(ClusterCount/8) — a too-short bitmap silently marks the
     * uncovered clusters "used" and loses most of the volume. */
    if (bitmap_len < (((uint64_t)fs->cluster_count + 7) / 8) ||
        bitmap_len > (uint64_t)fs->cluster_count) return -1;
    if (upcase_len < 2 || (upcase_len & 1) || upcase_len > (uint64_t)(256 * 1024)) return -1;

    /* Allocation bitmap. */
    fs->bitmap_bytes = (uint32_t)bitmap_len;
    fs->bitmap_cluster = bitmap_cluster;
    fs->bitmap_no_fat_chain = (bitmap_flags & EXFAT_FLAG_NO_FAT_CHAIN) ? 1 : 0;
    fs->bitmap = kmalloc(fs->bitmap_bytes);
    if (!fs->bitmap) return -1;
    if (exfat_read_stream(fs, bitmap_cluster, fs->bitmap_no_fat_chain,
                          bitmap_len, fs->bitmap) != 0) {
        kfree(fs->bitmap, fs->bitmap_bytes);
        fs->bitmap = NULL;
        return -1;
    }
    /* audit M9: count free (clear) clusters by byte-popcount over the covering
     * bitmap bytes instead of a per-cluster loop that spins up to ~2^32 times
     * on a large volume.  Only the first ClusterCount bits are real clusters;
     * trailing bits in the final byte are reserved and must not be counted. */
    fs->free_clusters = 0;
    uint32_t full_bytes = fs->cluster_count / 8;
    for (uint32_t i = 0; i < full_bytes; i++)
        fs->free_clusters += 8u - exfat_popcount8(fs->bitmap[i]);
    uint32_t rem_bits = fs->cluster_count & 7;
    if (rem_bits) {
        uint8_t last = fs->bitmap[full_bytes];
        for (uint32_t b = 0; b < rem_bits; b++)
            if (!((last >> b) & 1)) fs->free_clusters++;
    }

    /* Up-case table: identity by default, then apply the decompressed table. */
    fs->upcase = kmalloc(65536 * sizeof(uint16_t));
    if (!fs->upcase) { kfree(fs->bitmap, fs->bitmap_bytes); fs->bitmap = NULL; return -1; }
    for (uint32_t i = 0; i < 65536; i++) fs->upcase[i] = (uint16_t)i;

    uint8_t *table = kmalloc((uint32_t)upcase_len);
    if (!table) { kfree(fs->upcase, 65536 * sizeof(uint16_t)); fs->upcase = NULL;
                  kfree(fs->bitmap, fs->bitmap_bytes); fs->bitmap = NULL; return -1; }
    uint8_t nfc_up = (upcase_flags & EXFAT_FLAG_NO_FAT_CHAIN) ? 1 : 0;
    if (exfat_read_stream(fs, upcase_cluster, nfc_up, upcase_len, table) != 0) {
        kfree(table, (uint32_t)upcase_len);
        kfree(fs->upcase, 65536 * sizeof(uint16_t)); fs->upcase = NULL;
        kfree(fs->bitmap, fs->bitmap_bytes); fs->bitmap = NULL;
        return -1;
    }

    uint32_t nent = (uint32_t)(upcase_len / 2);
    uint32_t idx = 0;
    for (uint32_t i = 0; i < nent && idx < 65536; ) {
        uint16_t v = (uint16_t)(table[i * 2] | (table[i * 2 + 1] << 8));
        i++;
        if (v == 0xFFFF) {                       /* run of `count` identity mappings */
            if (i >= nent) break;
            uint16_t cnt = (uint16_t)(table[i * 2] | (table[i * 2 + 1] << 8));
            i++;
            idx += cnt;
        } else {
            fs->upcase[idx++] = v;
        }
    }
    kfree(table, (uint32_t)upcase_len);
    return 0;
}

/*
 * §3.4 Boot Checksum: 32-bit rotate-right sum over the 11 sectors of a boot
 * region, skipping the VolumeFlags (byte 106,107) and PercentInUse (byte 112)
 * fields, which implementations mutate without recomputing the checksum.
 */
static uint32_t exfat_boot_checksum(const uint8_t *sectors, uint32_t bytes_per_sector) {
    uint32_t n = bytes_per_sector * 11;
    uint32_t sum = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (i == 106 || i == 107 || i == 112) continue;
        sum = ((sum & 1) ? 0x80000000u : 0u) + (sum >> 1) + sectors[i];
    }
    return sum;
}

/* Read abstraction so the checksum verifier serves both the fs_node mount path
 * and the raw-blkdev read_label path.  Returns 0 on a full read. */
typedef int (*exfat_readfn)(void *ctx, uint64_t byte_off, uint32_t size, uint8_t *buf);

static int exfat_rd_node(void *ctx, uint64_t off, uint32_t size, uint8_t *buf) {
    fs_node_t *dev = (fs_node_t *)ctx;
    return (dev->read(dev, (off_t)off, size, buf) == size) ? 0 : -1;
}
static int exfat_rd_blk(void *ctx, uint64_t off, uint32_t size, uint8_t *buf) {
    return (blkdev_read_bytes((blkdev_t *)ctx, off, size, buf) == size) ? 0 : -1;
}

/*
 * Verify the §3.4 Boot Checksum of the boot region beginning at `base_sector`
 * (0 = main region, 12 = backup region).  The checksum sector is base_sector+11
 * and holds the repeating 4-byte checksum; comparing the first copy suffices.
 * Returns 0 on match.
 */
static int exfat_boot_region_ok(uint32_t bps, uint64_t base_sector,
                                exfat_readfn rd, void *ctx) {
    uint32_t region = bps * 11;
    uint8_t *buf = kmalloc(region);
    if (!buf) return -1;
    uint8_t ck[4];
    int rc = -1;
    if (rd(ctx, base_sector * bps, region, buf) == 0 &&
        rd(ctx, (base_sector + 11) * bps, 4, ck) == 0) {
        uint32_t want = (uint32_t)ck[0] | ((uint32_t)ck[1] << 8) |
                        ((uint32_t)ck[2] << 16) | ((uint32_t)ck[3] << 24);
        rc = (exfat_boot_checksum(buf, bps) == want) ? 0 : -1;
    }
    kfree(buf, region);
    return rc;
}

/* Parse + validate the boot region shared by mount and read_label. */
static int exfat_parse_boot(const uint8_t *boot, exfat_boot_t *out) {
    const exfat_boot_t *b = (const exfat_boot_t *)boot;
    if (memcmp(b->fs_name, "EXFAT   ", 8) != 0) return -1;   /* not exFAT */
    if (b->bytes_per_sector_shift < 9 || b->bytes_per_sector_shift > 12) return -1;
    if (b->sectors_per_cluster_shift > 25) return -1;
    if ((uint32_t)b->bytes_per_sector_shift + b->sectors_per_cluster_shift > 25) return -1;
    if (b->cluster_count == 0) return -1;
    if (b->root_cluster < EXFAT_FIRST_CLUSTER ||
        b->root_cluster >= EXFAT_FIRST_CLUSTER + b->cluster_count) return -1;

    /*
     * audit M3/M9/L3/L10/L11: the fields above were the only ones validated,
     * so a corrupt or hostile boot sector could carry geometry that directs
     * FAT/data writes into the boot region or off the end of the volume, or an
     * absurd ClusterCount that drives ~2^32-iteration allocation scans.  Cross-
     * check the §3.1 layout inequalities and version/FAT-count constraints.
     */
    uint32_t bps       = 1u << b->bytes_per_sector_shift;
    uint8_t  spc_shift = b->sectors_per_cluster_shift;
    uint64_t vol       = b->volume_length;
    uint64_t fat_off   = b->fat_offset;
    uint64_t fat_len   = b->fat_length;
    uint64_t heap_off  = b->cluster_heap_offset;
    uint64_t ccount    = b->cluster_count;
    uint32_t nfat      = b->num_fats;

    /* §3.1.12: shall not mount a major revision other than 1. */
    if ((b->fs_revision >> 8) != 1) return -1;
    /* §3.1.16: NumberOfFats is 1 (2 only for TexFAT, whose second FAT/bitmap
     * this driver does not maintain — refuse rather than corrupt it). */
    if (nfat != 1) return -1;
    /* §3.1.13.1: with a single FAT the active FAT must be the first. */
    if (b->volume_flags & EXFAT_VOLFLAG_ACTIVE_FAT) return -1;
    /* §3.1.9: ClusterCount+1 must not exceed 0xFFFFFFF6, so every FAT sentinel
     * (>= 0xFFFFFFF7) stays outside the valid cluster-index range. */
    if (ccount > 0xFFFFFFF5ULL) return -1;
    /* §3.1.5: volume is at least 1 MB of sectors. */
    if (vol < ((1ULL << 20) / bps)) return -1;
    /* §3.1.6: FatOffset accounts for the main+backup boot regions. */
    if (fat_off < 24) return -1;
    /* §3.1.7: each FAT must be large enough to describe every cluster. */
    if (fat_len < (((ccount + 2) * 4) + bps - 1) / bps) return -1;
    /* §3.1.8: the Cluster Heap begins after all FATs. */
    if (fat_off + fat_len * nfat > heap_off) return -1;
    /* §3.1.9: the whole Cluster Heap fits within the volume. */
    if (heap_off + (ccount << spc_shift) > vol) return -1;

    memcpy(out, b, sizeof(*out));
    return 0;
}

static fs_node_t *exfat_mount(const char *device, uint32_t flags, void *data) {
    (void)device; (void)flags;
    fs_node_t *dev = (fs_node_t *)data;
    if (!dev || !dev->read) {
        kprint("exFAT: no device or read function\n");
        return NULL;
    }

    /* The boot region is defined at byte offsets < 512, before the sector
     * size is even known, so a fixed 512-byte read is correct. */
    uint8_t boot[512];
    if (dev->read(dev, 0, sizeof(boot), boot) != sizeof(boot)) return NULL;

    /* audit M11: verify the §3.4 Boot Checksum before trusting any geometry.
     * BytesPerSectorShift (needed to size the region read) lives at a fixed
     * offset inside the first 512 bytes; validate it, then checksum the main
     * region and fall back to the backup boot region (sectors 12-23). */
    uint8_t bps_shift = boot[offsetof(exfat_boot_t, bytes_per_sector_shift)];
    if (bps_shift < 9 || bps_shift > 12) return NULL;
    uint32_t bps = 1u << bps_shift;

    exfat_boot_t b;
    if (exfat_boot_region_ok(bps, 0, exfat_rd_node, dev) == 0) {
        if (exfat_parse_boot(boot, &b) != 0) return NULL;
    } else if (exfat_boot_region_ok(bps, 12, exfat_rd_node, dev) == 0) {
        uint8_t bboot[512];
        if (dev->read(dev, (off_t)(12u * bps), sizeof(bboot), bboot) != sizeof(bboot))
            return NULL;
        if (exfat_parse_boot(bboot, &b) != 0) return NULL;
        kprint("exFAT: main boot region checksum bad, using backup\n");
    } else {
        kprint("exFAT: boot checksum verification failed\n");
        return NULL;
    }

    exfat_fs_t *fs = kmalloc(sizeof(exfat_fs_t));
    if (!fs) return NULL;
    memset(fs, 0, sizeof(*fs));
    mutex_init(&fs->lock, "exfat_fs");
    fs->device = dev;
    fs->bytes_per_sector_shift = b.bytes_per_sector_shift;
    fs->sectors_per_cluster_shift = b.sectors_per_cluster_shift;
    fs->bytes_per_sector = 1u << b.bytes_per_sector_shift;
    fs->sectors_per_cluster = 1u << b.sectors_per_cluster_shift;
    fs->cluster_size = fs->bytes_per_sector << b.sectors_per_cluster_shift;
    fs->fat_offset = b.fat_offset;
    fs->fat_length = b.fat_length;
    fs->cluster_heap_offset = b.cluster_heap_offset;
    fs->cluster_count = b.cluster_count;
    fs->root_cluster = b.root_cluster;
    fs->volume_length = b.volume_length;

    if (fs->cluster_size == 0 || fs->cluster_size > EXFAT_MAX_CLUSTER_SIZE) {
        kfree(fs, sizeof(*fs));
        return NULL;
    }

    /* Load the allocation bitmap + up-case table from the root directory. */
    if (exfat_load_metadata(fs) != 0) {
        kprint("exFAT: failed to load bitmap/up-case metadata\n");
        if (fs->bitmap) kfree(fs->bitmap, fs->bitmap_bytes);
        if (fs->upcase) kfree(fs->upcase, 65536 * sizeof(uint16_t));
        kfree(fs, sizeof(*fs));
        return NULL;
    }

    /* Root directory: a normal FAT-chained directory starting at root_cluster.
     * has_dir_entry = 0 — the root has no parent entry to update. */
    fs_node_t *root = exfat_alloc_node(fs, "/", EXFAT_ROOT_INO, fs->root_cluster,
                                       0 /* size unknown; bounded by EOD */,
                                       EXFAT_ATTR_DIRECTORY, 0 /* uses FAT chain */,
                                       0, 0, 0,
                                       0, 0, 0, 0, 0);
    if (!root) {
        if (fs->bitmap) kfree(fs->bitmap, fs->bitmap_bytes);
        if (fs->upcase) kfree(fs->upcase, 65536 * sizeof(uint16_t));
        kfree(fs, sizeof(*fs));
        return NULL;
    }
    root->unmount = exfat_unmount;
    /* Pin the root for the whole mount lifetime (audit H1): keyed by
     * (fs, EXFAT_ROOT_INO), it is now an ordinary cache slot, so without a pin
     * the round-robin would eventually recycle it out from under the VFS. */
    exfat_node_open(root);
    fs->root_node = root;
    /* audit M7/L9: warn on a dirty volume, mark our read-write session dirty,
     * and set PercentInUse "not available" since we don't track it precisely. */
    if (b.volume_flags & EXFAT_VOLFLAG_DIRTY)
        kprint("exFAT: volume was not cleanly unmounted (VolumeDirty set)\n");
    exfat_set_volume_dirty(fs, 1);
    exfat_set_percent_unknown(fs);
    kprint("exFAT: mounted volume (read-write)\n");
    return root;
}

/* Read the volume label from a raw block device without mounting. */
int exfat_read_label(blkdev_t *dev, char *label, size_t len) {
    if (!dev || !label || len == 0) return -1;

    uint8_t boot[512];
    if (blkdev_read_bytes(dev, 0, sizeof(boot), boot) != sizeof(boot)) return -1;

    /* audit M11: verify the §3.4 Boot Checksum (main, then backup) before
     * trusting the geometry this probe walks. */
    uint8_t bps_shift = boot[offsetof(exfat_boot_t, bytes_per_sector_shift)];
    if (bps_shift < 9 || bps_shift > 12) return -1;
    uint32_t region_bps = 1u << bps_shift;
    exfat_boot_t b;
    if (exfat_boot_region_ok(region_bps, 0, exfat_rd_blk, dev) == 0) {
        if (exfat_parse_boot(boot, &b) != 0) return -1;
    } else if (exfat_boot_region_ok(region_bps, 12, exfat_rd_blk, dev) == 0) {
        uint8_t bboot[512];
        if (blkdev_read_bytes(dev, (uint64_t)12u * region_bps, sizeof(bboot), bboot)
                != sizeof(bboot)) return -1;
        if (exfat_parse_boot(bboot, &b) != 0) return -1;
    } else {
        return -1;
    }

    uint32_t bps = 1u << b.bytes_per_sector_shift;
    uint32_t spc = 1u << b.sectors_per_cluster_shift;
    uint32_t cluster_size = bps << b.sectors_per_cluster_shift;
    if (cluster_size == 0 || cluster_size > EXFAT_MAX_CLUSTER_SIZE) return -1;

    uint8_t *cbuf = kmalloc(cluster_size);
    if (!cbuf) return -1;

    int rc = -1;
    uint32_t cluster = b.root_cluster;
    int guard = 0;
    while (cluster >= EXFAT_FIRST_CLUSTER &&
           cluster < EXFAT_FIRST_CLUSTER + b.cluster_count && guard++ < 4096) {
        uint64_t sector = b.cluster_heap_offset +
                          (uint64_t)(cluster - EXFAT_FIRST_CLUSTER) * spc;
        if (blkdev_read_bytes(dev, sector * bps, cluster_size, cbuf) != cluster_size)
            break;

        for (uint32_t i = 0; i + 32 <= cluster_size; i += 32) {
            uint8_t type = cbuf[i];
            if (type == EXFAT_ENTRY_EOD) goto done;   /* end of dir, no label */
            if (type == EXFAT_ENTRY_LABEL) {
                const exfat_label_entry_t *le = (const exfat_label_entry_t *)(cbuf + i);
                int n = le->char_count;
                if (n <= 0) goto done;                /* empty label */
                if (n > 11) n = 11;
                /* le->label is a packed field; pass its bytes directly. */
                exfat_utf16_to_utf8(cbuf + i + offsetof(exfat_label_entry_t, label),
                                    n, label, len);
                rc = 0;
                goto done;
            }
        }
        /* Follow the FAT chain to the next root-directory cluster. */
        uint32_t next = 0xFFFFFFFFU;
        if (blkdev_read_bytes(dev, (uint64_t)b.fat_offset * bps + (uint64_t)cluster * 4,
                              4, &next) != 4)
            break;
        if (next < EXFAT_FIRST_CLUSTER || next >= EXFAT_CLUSTER_END) break;
        cluster = next;
    }
done:
    kfree(cbuf, cluster_size);
    return rc;
}

static filesystem_t exfat_filesystem = {
    .name = "exfat",
    .mount = exfat_mount,
    .read_label = exfat_read_label,
};

void exfat_init(void) {
    kprint("Initializing exFAT Driver...\n");
    mutex_init(&exfat_node_cache_lock, "exfat_ncache");
    vfs_register_filesystem(&exfat_filesystem);
}
