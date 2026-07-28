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
 */
static uint32_t exfat_chain_nth(exfat_fs_t *fs, uint32_t start, int no_fat_chain, uint32_t n) {
    if (!exfat_cluster_valid(fs, start)) return 0;
    if (no_fat_chain) {
        uint32_t c = start + n;
        return exfat_cluster_valid(fs, c) ? c : 0;
    }
    uint32_t c = start;
    for (uint32_t i = 0; i < n; i++) {
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

/* Mark a cluster used/free in memory and flush the affected byte. */
static int exfat_bitmap_set(exfat_fs_t *fs, uint32_t cluster, int used) {
    uint32_t bit = cluster - EXFAT_FIRST_CLUSTER;
    if (!fs->bitmap || (bit >> 3) >= fs->bitmap_bytes) return -1;
    uint8_t *b = &fs->bitmap[bit >> 3];
    int was = (*b >> (bit & 7)) & 1;
    if (used) *b |= (uint8_t)(1u << (bit & 7));
    else      *b &= (uint8_t)~(1u << (bit & 7));
    if (was && !used) fs->free_clusters++;
    if (!was && used && fs->free_clusters) fs->free_clusters--;
    return exfat_bitmap_flush_bit(fs, bit);
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
    /* Slot 0 is permanently the root node: fs->root_node is held for the whole
     * mount lifetime, and every path lookup starts by dereferencing it.  The
     * old code round-robined it like any other slot, so after
     * EXFAT_NODE_CACHE_SIZE lookups the round-robin recycled the root to some
     * other file -> the root (and other VFS-held nodes) silently became a
     * different, often non-directory, inode.  That is what broke reading back
     * anything substantial (untar onto exFAT failed with ENOTDIR, `ls` came up
     * empty).  Pin the root to slot 0 and round-robin only the rest. */
    uint32_t idx;
    if (inode == EXFAT_ROOT_INO) {
        idx = 0;
    } else {
        idx = 1 + (exfat_node_cache_idx++ % (EXFAT_NODE_CACHE_SIZE - 1));
    }
    exfat_node_t *ctx = &exfat_node_cache[idx];
    fs_node_t *node = &exfat_fs_node_cache[idx];
    memset(ctx, 0, sizeof(*ctx));
    memset(node, 0, sizeof(*node));

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
    if (exfat_read_cluster(it->fs, c, it->buf) != 0) { it->eof = 1; return -1; }
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
        if (type != EXFAT_ENTRY_FILE) continue;     /* only 0x85 starts a set */

        /* Copy primary fields out before the buffer can be reloaded. */
        const exfat_file_entry_t *fe = (const exfat_file_entry_t *)e;
        uint16_t attr = fe->file_attributes;
        uint8_t  secondary_count = fe->secondary_count;
        int64_t crt = exfat_time(fe->create_time);
        int64_t mod = exfat_time(fe->modify_time);
        int64_t acc = exfat_time(fe->access_time);

        /* Secondary #1 must be the stream extension (0xC0). */
        const uint8_t *se = exfat_dir_entry(&it, ei + 1);
        if (!se || se[0] != EXFAT_ENTRY_STREAM) continue;
        const exfat_stream_entry_t *st = (const exfat_stream_entry_t *)se;
        uint8_t  nlen   = st->name_length;
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
    uint32_t within = (uint32_t)(off % fs->cluster_size);
    uint32_t cluster = exfat_chain_nth(fs, ctx->first_cluster, ctx->no_fat_chain,
                                       (uint32_t)(off / fs->cluster_size));

    while (done < size && cluster != 0) {
        if (exfat_read_cluster(fs, cluster, cbuf) != 0) break;
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

static uint32_t exfat_dir_extend(exfat_node_t *dir) {
    exfat_fs_t *fs = dir->fs;
    if (dir->no_fat_chain) return 0;                 /* not supported */
    if (!exfat_cluster_valid(fs, dir->first_cluster)) return 0;

    uint32_t nc = exfat_alloc_cluster(fs);
    if (nc == 0) return 0;

    uint8_t *z = kmalloc(fs->cluster_size);
    if (!z) { exfat_free_cluster(fs, nc); return 0; }
    memset(z, 0, fs->cluster_size);
    if (exfat_write_cluster(fs, nc, z) != 0) {
        kfree(z, fs->cluster_size);
        exfat_free_cluster(fs, nc);
        return 0;
    }
    kfree(z, fs->cluster_size);

    /* Link onto the end of the chain. */
    uint32_t last = dir->first_cluster;
    uint32_t guard = 0;
    while (guard++ < fs->cluster_count) {
        uint32_t x = exfat_fat_next(fs, last);
        if (x < EXFAT_FIRST_CLUSTER || x >= EXFAT_CLUSTER_END) break;
        last = x;
    }
    exfat_fat_set(fs, last, nc);
    exfat_fat_set(fs, nc, EXFAT_CLUSTER_EOF);

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
            kfree(set, nbytes);
            return -EIO;
        }
    }
    kfree(set, nbytes);
    if (out_primary)   *out_primary = idx;
    if (out_secondary) *out_secondary = (uint8_t)secondary;
    return 0;
}

/* Delete an entry set: clear the "in use" bit on each of its entries. */
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
        type &= (uint8_t)~EXFAT_ENTRY_INUSE;      /* 0x85->0x05, 0xC0->0x40, 0xC1->0x41 */
        if (exfat_write_bytes(fs, off, 1, &type) != 0) return -EIO;
    }
    return 0;
}

/* ------------------------------------------------------------ file writes */

static size_t exfat_file_write(fs_node_t *node, off_t offset, size_t size,
                               const uint8_t *buffer) {
    exfat_node_t *ctx = (exfat_node_t *)(uintptr_t)node->impl;
    if (!ctx || !buffer || offset < 0 || size == 0) return 0;
    exfat_fs_t *fs = ctx->fs;
    uint32_t cs = fs->cluster_size;

    uint64_t end = (uint64_t)offset + size;
    uint32_t need_clusters = (uint32_t)((end + cs - 1) / cs);
    uint32_t have_clusters = (uint32_t)((ctx->size + cs - 1) / cs);

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

    /* Allocate the first cluster if the file is empty. */
    if (!exfat_cluster_valid(fs, ctx->first_cluster)) {
        uint32_t nc = exfat_alloc_cluster(fs);
        if (nc == 0) return 0;
        ctx->first_cluster = nc;
        ctx->no_fat_chain = 0;
    }

    /* Walk to the need_clusters-th cluster, extending the FAT chain as needed. */
    uint32_t cluster = ctx->first_cluster;
    for (uint32_t i = 1; i < need_clusters; i++) {
        uint32_t nx;
        if (ctx->no_fat_chain) {
            nx = cluster + 1;                    /* contiguous, already allocated */
        } else {
            nx = exfat_fat_next(fs, cluster);
            if (nx < EXFAT_FIRST_CLUSTER || nx >= EXFAT_CLUSTER_END) {
                nx = exfat_alloc_cluster(fs);
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
                                 (uint32_t)((uint64_t)offset / cs));
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
    exfat_update_stream(ctx, ctx->first_cluster, ctx->no_fat_chain, ctx->size);
    return done;
}

static int exfat_truncate(fs_node_t *node, off_t new_size) {
    exfat_node_t *ctx = (exfat_node_t *)(uintptr_t)node->impl;
    if (!ctx || new_size < 0) return -EINVAL;
    exfat_fs_t *fs = ctx->fs;
    uint32_t cs = fs->cluster_size;
    uint64_t ns = (uint64_t)new_size;
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
        /* Grow: allocate the extra clusters so DataLength stays backed. */
        if (ctx->no_fat_chain && exfat_cluster_valid(fs, ctx->first_cluster)) {
            for (uint32_t i = 0; i < have; i++) {
                uint32_t cc = ctx->first_cluster + i;
                exfat_fat_set(fs, cc, (i + 1 < have) ? (cc + 1) : EXFAT_CLUSTER_EOF);
            }
            ctx->no_fat_chain = 0;
        }
        if (!exfat_cluster_valid(fs, ctx->first_cluster)) {
            uint32_t nc = exfat_alloc_cluster(fs);
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
            uint32_t nc = exfat_alloc_cluster(fs);
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

static int exfat_mkdir(fs_node_t *parent, const char *name, uint16_t perm) {
    (void)perm;
    exfat_node_t *dir = (exfat_node_t *)(uintptr_t)parent->impl;
    if (!dir || !name || !name[0]) return -EINVAL;
    if ((parent->flags & 0x7) != FS_DIRECTORY) return -ENOTDIR;
    if (!strcmp(name, ".") || !strcmp(name, "..")) return -EINVAL;
    exfat_fs_t *fs = dir->fs;

    struct exfat_dirinfo info;
    if (exfat_scan_dir(dir, name, 0, &info) == 0) return -EEXIST;

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

static int exfat_mknod(fs_node_t *parent, const char *name, uint16_t mode, uint32_t dev) {
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
    if (exfat_scan_dir(dir, name, 0, &info) == 0) return -EEXIST;

    uint64_t pidx; uint8_t sec;
    return exfat_create_set(dir, name, EXFAT_ATTR_ARCHIVE, 0, 0, 0, &pidx, &sec);
}

static int exfat_unlink(fs_node_t *parent, const char *name) {
    exfat_node_t *dir = (exfat_node_t *)(uintptr_t)parent->impl;
    if (!dir || !name) return -EINVAL;
    exfat_fs_t *fs = dir->fs;

    struct exfat_dirinfo info;
    int rc = exfat_scan_dir(dir, name, 0, &info);
    if (rc != 0) return rc;
    if (info.attr & EXFAT_ATTR_DIRECTORY) return -EISDIR;

    if (exfat_cluster_valid(fs, info.first_cluster))
        exfat_free_chain(fs, info.first_cluster, info.no_fat_chain, info.size);
    return exfat_delete_set(dir, info.dir_entry_index, info.secondary_count);
}

static int exfat_rmdir(fs_node_t *parent, const char *name) {
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
    if (exfat_scan_dir(&tmp, NULL, 0, &child) == 0) return -ENOTEMPTY;

    if (exfat_cluster_valid(fs, info.first_cluster))
        exfat_free_chain(fs, info.first_cluster, info.no_fat_chain, info.size);
    return exfat_delete_set(dir, info.dir_entry_index, info.secondary_count);
}

static int exfat_rename(fs_node_t *old_parent, const char *old_name,
                        fs_node_t *new_parent, const char *new_name) {
    exfat_node_t *odir = (exfat_node_t *)(uintptr_t)old_parent->impl;
    exfat_node_t *ndir = (exfat_node_t *)(uintptr_t)new_parent->impl;
    if (!odir || !ndir || !old_name || !new_name) return -EINVAL;
    exfat_fs_t *fs = odir->fs;

    struct exfat_dirinfo src;
    int rc = exfat_scan_dir(odir, old_name, 0, &src);
    if (rc != 0) return rc;

    /* Remove an existing destination first (files freed; dirs must be empty). */
    struct exfat_dirinfo dst;
    if (exfat_scan_dir(ndir, new_name, 0, &dst) == 0) {
        if (dst.attr & EXFAT_ATTR_DIRECTORY) {
            exfat_node_t tmp;
            memset(&tmp, 0, sizeof(tmp));
            tmp.fs = fs;
            tmp.first_cluster = dst.first_cluster;
            tmp.no_fat_chain = dst.no_fat_chain;
            struct exfat_dirinfo dchild;
            if (exfat_scan_dir(&tmp, NULL, 0, &dchild) == 0) return -ENOTEMPTY;
        }
        if (exfat_cluster_valid(fs, dst.first_cluster))
            exfat_free_chain(fs, dst.first_cluster, dst.no_fat_chain, dst.size);
        exfat_delete_set(ndir, dst.dir_entry_index, dst.secondary_count);
    }

    /* Write a new set in the target directory pointing at the same clusters. */
    uint64_t pidx; uint8_t sec;
    rc = exfat_create_set(ndir, new_name, src.attr, src.first_cluster,
                          src.size, src.no_fat_chain, &pidx, &sec);
    if (rc != 0) return rc;

    /* Remove the old set without freeing the (reused) cluster chain.  The old
     * set is still live, so create_set could not have overwritten it. */
    return exfat_delete_set(odir, src.dir_entry_index, src.secondary_count);
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
    if (!root) return -1;
    exfat_node_t *ctx = (exfat_node_t *)(uintptr_t)root->impl;
    if (!ctx || !ctx->fs) return -1;
    exfat_fs_t *fs = ctx->fs;

    for (uint32_t i = 0; i < EXFAT_NODE_CACHE_SIZE; i++) {
        if (exfat_node_cache[i].fs == fs) {
            memset(&exfat_node_cache[i], 0, sizeof(exfat_node_t));
            memset(&exfat_fs_node_cache[i], 0, sizeof(fs_node_t));
        }
    }
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
    if (bitmap_len == 0 || bitmap_len > (uint64_t)fs->cluster_count) return -1;
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
    fs->free_clusters = 0;
    for (uint32_t c = EXFAT_FIRST_CLUSTER; c < EXFAT_FIRST_CLUSTER + fs->cluster_count; c++)
        if (!exfat_bitmap_test(fs, c)) fs->free_clusters++;

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

    exfat_boot_t b;
    if (exfat_parse_boot(boot, &b) != 0) return NULL;   /* not a valid exFAT volume */

    exfat_fs_t *fs = kmalloc(sizeof(exfat_fs_t));
    if (!fs) return NULL;
    memset(fs, 0, sizeof(*fs));
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
    fs->root_node = root;
    kprint("exFAT: mounted volume (read-write)\n");
    return root;
}

/* Read the volume label from a raw block device without mounting. */
int exfat_read_label(blkdev_t *dev, char *label, size_t len) {
    if (!dev || !label || len == 0) return -1;

    uint8_t boot[512];
    if (blkdev_read_bytes(dev, 0, sizeof(boot), boot) != sizeof(boot)) return -1;
    exfat_boot_t b;
    if (exfat_parse_boot(boot, &b) != 0) return -1;

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
    vfs_register_filesystem(&exfat_filesystem);
}
