/*
 * sys/fs/sysv/sysv.c — read-only mount for V7-derived filesystems
 * (Xenix / SystemV / Coherent).
 *
 * Mount is the only side that probes for a variant — we read the
 * 1024-byte superblock at disk offset 512, check the magic at
 * offset 0x1F8, and decide between Xenix and SysV based on the
 * value.  Block size comes from the "type" word at 0x1FC (1 = 512,
 * 2 = 1024, 3 = 2048).
 *
 * Read paths shared across variants: inode lookup (4 inodes per
 * 256-byte chunk; SYSV_ROOT_INO at the start of the inode table),
 * zone unpacking (3-byte little-endian), file_read (direct +
 * indirect + double-indirect + triple-indirect), readdir / finddir
 * (14-char names, fixed 16-byte dirent).
 *
 * Write paths: none.  This driver is read-only; sysv_unmount
 * releases per-mount state and leaves the device alone.
 */

#include <fs/sysv/sysv.h>
#include <string.h>
#include <stdio.h>
#include <vm/vm_kmem.h>
#include <kern/console.h>
#include <sys/statfs.h>
#include <sys/dirent.h>
#include <sys/stat.h>

/* ===================================================================
 * Forward decls
 * =================================================================== */
/* sysv_mount is declared (non-static) in sysv.h so the VFS init
 * path can call it indirectly; the unmount callback is module-
 * local. */
static int              sysv_unmount(fs_node_t *root);
static int              sysv_read_inode(sysv_fs_t *fs, uint32_t ino, fs_node_t *node);
static fs_node_t       *sysv_alloc_node(sysv_fs_t *fs, uint32_t ino);
static size_t           sysv_file_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer);
static struct dirent   *sysv_readdir(fs_node_t *node, uint64_t index);
static fs_node_t       *sysv_finddir(fs_node_t *node, char *name);

static filesystem_t sysv_filesystem = {
    .name = "sysv",
    .mount = sysv_mount,
    .caps  = VFS_CAP_RDONLY_ONLY,
    .next  = NULL,
};

static filesystem_t xenix_filesystem = {
    .name = "xenix",
    .mount = sysv_mount,
    .caps  = VFS_CAP_RDONLY_ONLY,
    .next  = NULL,
};

void sysv_init(void) {
    vfs_register_filesystem(&sysv_filesystem);
    vfs_register_filesystem(&xenix_filesystem);
}

/* ===================================================================
 * Low-level helpers
 * =================================================================== */

/*
 * Unpack a 3-byte little-endian zone number (V7 / Xenix
 * convention).  The high byte sits in i_addr[base+2]; bytes 0..1
 * are the low 16 bits little-endian.  Returns 0 for the "no block"
 * encoding (all three bytes zero).
 */
static inline uint32_t sysv_unpack_zone(const uint8_t *p) {
    return ((uint32_t)p[0])
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16);
}

/*
 * Read `nbytes` from `block_no * fs->block_size + within` of the
 * backing device.  Returns nbytes on success or 0 on read error /
 * out-of-range.
 */
static size_t sysv_read_block_range(sysv_fs_t *fs, uint32_t block_no,
                                    uint32_t within, void *buf, size_t nbytes) {
    if (!fs || !fs->device || !buf || nbytes == 0) return 0;
    if (block_no == 0) {
        /* Block 0 is the boot block; reads of "block 0" usually
         * indicate a sparse hole — return zeros to match V7
         * semantics for files with holes. */
        memset(buf, 0, nbytes);
        return nbytes;
    }
    off_t off = (off_t)block_no * (off_t)fs->block_size + (off_t)within;
    return read_fs(fs->device, off, nbytes, buf);
}

/*
 * Resolve a logical block index within a file to its physical
 * block number.  Walks direct → single-indirect → double-indirect
 * → triple-indirect.  Returns 0 if the index falls in a hole.
 */
static uint32_t sysv_resolve_block(sysv_fs_t *fs, sysv_node_t *node,
                                   uint32_t logical) {
    /* Pointers per indirect block (3-byte zone numbers). */
    const uint32_t ppb = fs->block_size / 3;

    if (logical < 10) {
        return node->addr[logical];
    }
    logical -= 10;

    /* Helper closure: read the i-th 3-byte entry from a given
     * physical block. */
    uint8_t indir[2048];   /* big enough for the largest block size we accept */
    if (fs->block_size > sizeof(indir)) return 0;

    if (logical < ppb) {
        /* single indirect at node->addr[10] */
        if (node->addr[10] == 0) return 0;
        if (sysv_read_block_range(fs, node->addr[10], 0, indir, fs->block_size) != fs->block_size)
            return 0;
        return sysv_unpack_zone(indir + logical * 3);
    }
    logical -= ppb;

    if (logical < ppb * ppb) {
        /* double indirect at node->addr[11] */
        if (node->addr[11] == 0) return 0;
        if (sysv_read_block_range(fs, node->addr[11], 0, indir, fs->block_size) != fs->block_size)
            return 0;
        uint32_t mid = sysv_unpack_zone(indir + (logical / ppb) * 3);
        if (mid == 0) return 0;
        if (sysv_read_block_range(fs, mid, 0, indir, fs->block_size) != fs->block_size)
            return 0;
        return sysv_unpack_zone(indir + (logical % ppb) * 3);
    }
    logical -= ppb * ppb;

    /* triple indirect at node->addr[12] */
    if (node->addr[12] == 0) return 0;
    if (sysv_read_block_range(fs, node->addr[12], 0, indir, fs->block_size) != fs->block_size)
        return 0;
    uint32_t l1 = sysv_unpack_zone(indir + (logical / (ppb * ppb)) * 3);
    if (l1 == 0) return 0;
    if (sysv_read_block_range(fs, l1, 0, indir, fs->block_size) != fs->block_size)
        return 0;
    uint32_t l2 = sysv_unpack_zone(indir + ((logical / ppb) % ppb) * 3);
    if (l2 == 0) return 0;
    if (sysv_read_block_range(fs, l2, 0, indir, fs->block_size) != fs->block_size)
        return 0;
    return sysv_unpack_zone(indir + (logical % ppb) * 3);
}

/* ===================================================================
 * Inode read
 * =================================================================== */
static int sysv_read_inode(sysv_fs_t *fs, uint32_t ino, fs_node_t *node) {
    if (!fs || !node || ino == 0) return -1;

    /* Inodes are 64 bytes; the inode table starts at
     * fs->inode_block_start (V7 convention: block 2). */
    const uint32_t inode_size = fs->inode_size;
    /* Inode N (1-based) sits at byte offset
     *   (N - 1) * inode_size  into the inode table. */
    off_t byte_off = (off_t)fs->inode_block_start * fs->block_size
                   + (off_t)(ino - 1) * inode_size;

    struct xenix_inode raw;
    if (read_fs(fs->device, byte_off, sizeof(raw), (uint8_t *)&raw) != sizeof(raw)) {
        return -1;
    }

    /* Lazy-allocate the per-node state. */
    sysv_node_t *nd = (sysv_node_t *)kmalloc(sizeof(*nd));
    if (!nd) return -1;
    memset(nd, 0, sizeof(*nd));
    nd->fs  = fs;
    nd->ino = ino;
    for (int i = 0; i < 13; i++) {
        nd->addr[i] = sysv_unpack_zone(&raw.i_addr[i * 3]);
    }
    node->ptr = (fs_node_t *)nd;        /* impl-private */
    node->impl = (uint32_t)(uintptr_t)fs;
    node->inode = ino;
    node->mask = raw.i_mode & 07777;
    node->uid  = raw.i_uid;
    node->gid  = raw.i_gid;
    node->length = raw.i_size;
    node->atime = raw.i_atime;
    node->mtime = raw.i_mtime;
    node->ctime = raw.i_ctime;
    /* mode -> flags: VFS uses bit constants on the low bits of
     * flags; mirror what minix/ext2 do. */
    uint16_t mode = raw.i_mode & 0170000;
    if (mode == 0040000)      node->flags = FS_DIRECTORY;
    else if (mode == 0120000) node->flags = FS_SYMLINK;
    else if (mode == 0020000) node->flags = FS_CHARDEVICE;
    else if (mode == 0060000) node->flags = FS_BLOCKDEVICE;
    else if (mode == 0010000) node->flags = FS_PIPE;
    else                       node->flags = FS_FILE;

    node->read    = sysv_file_read;
    node->readdir = (node->flags == FS_DIRECTORY) ? sysv_readdir : NULL;
    node->finddir = (node->flags == FS_DIRECTORY) ? sysv_finddir : NULL;
    return 0;
}

static fs_node_t *sysv_alloc_node(sysv_fs_t *fs, uint32_t ino) {
    fs_node_t *n = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    if (!n) return NULL;
    memset(n, 0, sizeof(*n));
    if (sysv_read_inode(fs, ino, n) != 0) {
        kfree(n, sizeof(*n));
        return NULL;
    }
    return n;
}

/* ===================================================================
 * Read
 * =================================================================== */
static size_t sysv_file_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    if (!node || !node->ptr || !buffer) return 0;
    sysv_node_t *nd = (sysv_node_t *)node->ptr;
    sysv_fs_t   *fs = nd->fs;
    if (offset < 0) return 0;
    if (offset >= node->length) return 0;
    if (offset + (off_t)size > node->length) size = (size_t)(node->length - offset);
    if (size == 0) return 0;

    size_t total = 0;
    uint8_t blkbuf[2048];
    if (fs->block_size > sizeof(blkbuf)) return 0;

    while (size > 0) {
        uint32_t lblk    = (uint32_t)(offset / fs->block_size);
        uint32_t within  = (uint32_t)(offset % fs->block_size);
        uint32_t pblk    = sysv_resolve_block(fs, nd, lblk);
        size_t   chunk   = fs->block_size - within;
        if (chunk > size) chunk = size;

        if (pblk == 0) {
            /* hole — read as zeros */
            memset(buffer, 0, chunk);
        } else {
            if (sysv_read_block_range(fs, pblk, 0, blkbuf, fs->block_size) != fs->block_size) {
                break;
            }
            memcpy(buffer, blkbuf + within, chunk);
        }

        buffer += chunk;
        offset += chunk;
        size   -= chunk;
        total  += chunk;
    }
    return total;
}

/* ===================================================================
 * Directory operations
 * =================================================================== */
static struct dirent g_dirent;

static struct dirent *sysv_readdir(fs_node_t *node, uint64_t index) {
    if (!node || !node->ptr) return NULL;
    sysv_node_t *nd = (sysv_node_t *)node->ptr;
    sysv_fs_t   *fs = nd->fs;
    off_t        off = (off_t)index * (off_t)sizeof(struct sysv_dirent);
    if (off >= node->length) return NULL;

    struct sysv_dirent de;
    if (sysv_file_read(node, off, sizeof(de), (uint8_t *)&de) != sizeof(de)) {
        return NULL;
    }
    if (de.d_ino == 0) {
        return NULL;
    }
    memset(&g_dirent, 0, sizeof(g_dirent));
    g_dirent.d_ino = de.d_ino;
    size_t copy = SYSV_NAMELEN;
    if (copy >= sizeof(g_dirent.d_name)) copy = sizeof(g_dirent.d_name) - 1;
    memcpy(g_dirent.d_name, de.d_name, copy);
    g_dirent.d_name[copy] = '\0';
    /* Trim trailing NULs (V7 dirent pads with NULs, not space). */
    for (size_t i = strlen(g_dirent.d_name); i > 0; i--) {
        if (g_dirent.d_name[i - 1] != '\0') break;
        g_dirent.d_name[i - 1] = '\0';
    }
    (void)fs;
    return &g_dirent;
}

static fs_node_t *sysv_finddir(fs_node_t *node, char *name) {
    if (!node || !node->ptr || !name) return NULL;
    sysv_node_t *nd = (sysv_node_t *)node->ptr;
    sysv_fs_t   *fs = nd->fs;

    off_t off = 0;
    while (off < node->length) {
        struct sysv_dirent de;
        if (sysv_file_read(node, off, sizeof(de), (uint8_t *)&de) != sizeof(de)) {
            return NULL;
        }
        off += (off_t)sizeof(de);
        if (de.d_ino == 0) continue;

        char dname[SYSV_NAMELEN + 1];
        memcpy(dname, de.d_name, SYSV_NAMELEN);
        dname[SYSV_NAMELEN] = '\0';
        if (strcmp(dname, name) == 0) {
            return sysv_alloc_node(fs, de.d_ino);
        }
    }
    return NULL;
}

/* ===================================================================
 * Mount / unmount
 * =================================================================== */
fs_node_t *sysv_mount(const char *device, uint32_t flags, void *data) {
    (void)device; (void)flags;
    fs_node_t *dev = (fs_node_t *)data;
    if (!dev) return NULL;

    /* The superblock is 1024 bytes at disk offset 512 for all
     * V7-derived layouts (block 1 in a 512-byte-block scheme; the
     * first half of block 1 in a 1024-byte scheme, with the
     * second half holding the start of the free-list cache).
     * Read the whole 1024 bytes so the magic at +0x1F8 and the
     * type word at +0x1FC are in our buffer. */
    uint8_t sb[1024];
    if (read_fs(dev, 512, sizeof(sb), sb) != sizeof(sb)) {
        return NULL;
    }

    uint32_t magic = ((uint32_t)sb[SYSV_MAGIC_OFFSET])
                   | ((uint32_t)sb[SYSV_MAGIC_OFFSET + 1] << 8)
                   | ((uint32_t)sb[SYSV_MAGIC_OFFSET + 2] << 16)
                   | ((uint32_t)sb[SYSV_MAGIC_OFFSET + 3] << 24);
    uint32_t type  = ((uint32_t)sb[SYSV_TYPE_OFFSET])
                   | ((uint32_t)sb[SYSV_TYPE_OFFSET + 1] << 8)
                   | ((uint32_t)sb[SYSV_TYPE_OFFSET + 2] << 16)
                   | ((uint32_t)sb[SYSV_TYPE_OFFSET + 3] << 24);

    sysv_variant_t variant;
    uint32_t       block_size;
    switch (magic) {
    case SYSV_MAGIC_XENIX: variant = SYSV_VARIANT_XENIX; break;
    case SYSV_MAGIC_SYSV4: variant = SYSV_VARIANT_SYSV4; break;
    default:
        return NULL;        /* unknown — let another driver have a go */
    }
    switch (type) {
    case 1: block_size = 512;  break;
    case 2: block_size = 1024; break;
    case 3: block_size = 2048; break;
    default:
        /* Old Xenix V1 disks sometimes have type=0; assume 512. */
        block_size = (variant == SYSV_VARIANT_XENIX) ? 512 : 1024;
        break;
    }

    sysv_fs_t *fs = (sysv_fs_t *)kmalloc(sizeof(*fs));
    if (!fs) return NULL;
    memset(fs, 0, sizeof(*fs));
    fs->device     = dev;
    fs->variant    = variant;
    fs->block_size = block_size;
    fs->inode_size = 64;
    fs->name_len   = SYSV_NAMELEN;
    /* The first two bytes of the SB are s_isize (inode-zone count
     * in zones, big-endian on some variants — but Xenix/SVR4 are
     * LE) and the next four are s_fsize.  We trust the LE
     * convention. */
    uint16_t s_isize = (uint16_t)sb[0] | ((uint16_t)sb[1] << 8);
    uint32_t s_fsize = (uint32_t)sb[2]
                     | ((uint32_t)sb[3] << 8)
                     | ((uint32_t)sb[4] << 16)
                     | ((uint32_t)sb[5] << 24);
    fs->nblocks          = s_fsize;
    fs->inode_block_start = 2;             /* boot=0, super=1, inodes=2.. */
    fs->ninodes          = (uint32_t)s_isize * (fs->block_size / fs->inode_size);
    fs->first_data_block = 2 + s_isize;

    kprintf("sysv: %s, %u-byte blocks, %u inodes, %u blocks total\n",
            variant == SYSV_VARIANT_XENIX ? "Xenix" : "SystemV",
            (unsigned)fs->block_size, (unsigned)fs->ninodes,
            (unsigned)fs->nblocks);

    fs_node_t *root = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    if (!root) {
        kfree(fs, sizeof(*fs));
        return NULL;
    }
    memset(root, 0, sizeof(*root));
    if (sysv_read_inode(fs, SYSV_ROOT_INO, root) != 0) {
        kfree(root, sizeof(*root));
        kfree(fs, sizeof(*fs));
        return NULL;
    }
    snprintf(root->name, sizeof(root->name), "sysv_root");
    root->unmount = sysv_unmount;
    return root;
}

static int sysv_unmount(fs_node_t *root) {
    if (!root) return -1;
    sysv_node_t *nd = (sysv_node_t *)root->ptr;
    if (nd) {
        sysv_fs_t *fs = nd->fs;
        kfree(nd, sizeof(*nd));
        if (fs) kfree(fs, sizeof(*fs));
    }
    kfree(root, sizeof(*root));
    return 0;
}
