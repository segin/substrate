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
#include <fs/exfat/exfat.h>
#include <drivers/storage/blkdev.h>
#include <kern/console.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/dirent.h>
#include <sys/mount.h>
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

/* ASCII case-insensitive equality (see the up-case-table note up top). */
static int exfat_name_eq(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 32);
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 32);
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == *b;
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

static uint32_t exfat_default_mask(uint16_t attr) {
    uint32_t m = (attr & EXFAT_ATTR_DIRECTORY) ? 0755U : 0644U;
    if (attr & EXFAT_ATTR_READ_ONLY) m &= ~0222U;
    return m;
}

/* -------------------------------------------------------------- node alloc */

static fs_node_t *exfat_alloc_node(exfat_fs_t *fs, const char *name, uint64_t inode,
                                   uint32_t first_cluster, uint64_t size,
                                   uint16_t attr, uint8_t no_fat_chain,
                                   int64_t crt, int64_t mod, int64_t acc) {
    uint32_t idx = exfat_node_cache_idx++ % EXFAT_NODE_CACHE_SIZE;
    exfat_node_t *ctx = &exfat_node_cache[idx];
    fs_node_t *node = &exfat_fs_node_cache[idx];
    memset(ctx, 0, sizeof(*ctx));
    memset(node, 0, sizeof(*node));

    ctx->fs = fs;
    ctx->first_cluster = first_cluster;
    ctx->size = size;
    ctx->attr = attr;
    ctx->no_fat_chain = no_fat_chain;

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
    } else {
        node->flags = FS_FILE;
        node->read = exfat_read;
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

        int match = want_name ? exfat_name_eq(utf8, want_name)
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
                            info.crt, info.mod, info.acc);
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

static int exfat_statfs(fs_node_t *node, struct statfs *buf) {
    if (!node || !buf) return -EINVAL;
    exfat_node_t *ctx = (exfat_node_t *)(uintptr_t)node->impl;
    if (!ctx || !ctx->fs) return -EINVAL;
    exfat_fs_t *fs = ctx->fs;

    memset(buf, 0, sizeof(*buf));
    buf->f_bsize  = fs->cluster_size;
    buf->f_iosize = fs->cluster_size;
    buf->f_blocks = fs->cluster_count;
    /* Free-cluster counting needs the allocation bitmap; this read-only driver
     * does not track it, so report a full volume (no writes are possible). */
    buf->f_bfree  = 0;
    buf->f_bavail = 0;
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
    kfree(fs, sizeof(exfat_fs_t));
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

    /* Root directory: a normal FAT-chained directory starting at root_cluster. */
    fs_node_t *root = exfat_alloc_node(fs, "/", EXFAT_ROOT_INO, fs->root_cluster,
                                       0 /* size unknown; bounded by EOD */,
                                       EXFAT_ATTR_DIRECTORY, 0 /* uses FAT chain */,
                                       0, 0, 0);
    if (!root) { kfree(fs, sizeof(*fs)); return NULL; }
    root->unmount = exfat_unmount;
    fs->root_node = root;
    kprint("exFAT: mounted volume (read-only)\n");
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
