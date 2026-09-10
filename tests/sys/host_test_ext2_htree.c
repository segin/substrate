/*
 * host_test_ext2_htree.c — exercise ext2 directory writes against a REAL
 * image whose directories carry EXT2_INDEX_FL (an htree).
 *
 * [EXT2-08] The driver used to refuse every mutation of an indexed
 * directory, so mkdir(2) inside one returned EOPNOTSUPP and autotools
 * could not create `src/.deps`.  This test drives the index-maintaining
 * write path: insert until leaves split and the tree grows an indirect
 * level, look every name back up through the index, then delete and
 * rename.  The structural verdict comes from `e2fsck -fn` in the
 * wrapper script — this program's job is to make the changes and prove
 * the driver can still find what it wrote.
 *
 * Usage: host_test_ext2_htree <image> <indexed-dir>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

typedef long off_t;
void kprint(const char *str) { (void)str; }

#include <sys/lock.h>
void mutex_init(mutex_t *m, const char *name) { m->locked = 0; m->name = name; }
void mutex_lock(mutex_t *m) { m->locked = 1; }
void mutex_unlock(mutex_t *m) { m->locked = 0; }

#include <vm/vm_kmem.h>
void *kmalloc(size_t size) { return calloc(1, size); }
void kfree(void *ptr, size_t size) { (void)size; free(ptr); }

#include <vfs/vfs.h>
void vfs_register_filesystem(filesystem_t *fs) { (void)fs; }

#include <vm/uma.h>
uma_zone_t *uma_zcreate(const char *name, size_t size, uma_ctor ctor, uma_dtor dtor,
                        uma_init init, uma_fini fini, int align, uint32_t flags) {
    (void)ctor; (void)dtor; (void)init; (void)fini; (void)align; (void)flags;
    uma_zone_t *z = calloc(1, sizeof(uma_zone_t));
    z->uz_name = name; z->uz_size = size; return z;
}
void uma_zdestroy(uma_zone_t *zone) { free(zone); }
void *uma_zalloc(uma_zone_t *zone, int flags) {
    (void)flags;
    return calloc(1, zone->uz_size > 0 ? zone->uz_size : 4096);
}
void uma_zfree(uma_zone_t *zone, void *item) { (void)zone; free(item); }

static uint8_t *disk;
static size_t   disk_len;

static size_t mock_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    if (offset < 0 || (size_t)offset + size > disk_len) return 0;
    memcpy(buffer, disk + offset, size);
    return size;
}
static size_t mock_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node;
    if (offset < 0 || (size_t)offset + size > disk_len) return 0;
    memcpy(disk + offset, buffer, size);
    return size;
}

#define get_time mock_get_time
long mock_get_time(void) { return 1757000000L; }

/* Real CRC implementations: e2fsck verifies these on any volume with
 * metadata_csum or uninit_bg, so stubbing them would make the test pass
 * while writing checksums no other implementation accepts. */
#include "../../sys/kern/crc32c.c"
#include "../../sys/kern/crc16.c"

int cmdline_debug_enabled(const char *what) { (void)what; return 0; }
int kprintf(const char *fmt, ...) { (void)fmt; return 0; }

#define vasprintf kernel_vasprintf
#include "../../sys/fs/ext2/ext2.c"
#include "../../sys/fs/ext2/ext2_hash.c"

/* Kernel globals the driver touches but this harness has no use for.
 * current_process stays NULL, which is the "no owner" path the driver
 * already handles (ext2_set_creator_owner / ext2_alloc_block both test
 * it before dereferencing). */
process_t *current_process = NULL;
unsigned long fs_open_count, fs_close_count;
size_t blkdev_read_bytes(blkdev_t *dev, uint64_t offset, size_t size, void *buffer) {
    (void)dev; (void)offset; (void)size; (void)buffer; return 0;
}
void blkdev_invalidate_node(fs_node_t *node) { (void)node; }
int ext2_xattr_get(fs_node_t *n, const char *k, void *v, size_t sz, size_t *out) {
    (void)n; (void)k; (void)v; (void)sz; (void)out; return -ENOTSUP;
}
int ext2_xattr_list(fs_node_t *n, void *out, size_t sz, size_t *used) {
    (void)n; (void)out; (void)sz; if (used) *used = 0; return 0;
}

#define FAIL(...) do { printf("FAILED: " __VA_ARGS__); return 1; } while (0)

/* How many entries to add.  A 1 KiB-block leaf holds roughly 60 short
 * names and the root indexes ~124 of them, so the default exercises
 * leaf splitting only.  Set HTREE_ADD high enough to overflow the root
 * (order 10^4 at 1 KiB blocks) and the run also covers ext2_dx_grow()
 * and ext2_dx_split_node() — the tree gaining an indirect level. */
#define NADD_DEFAULT 1500

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("usage: %s <image> <dir> [--allow-linear]\n", argv[0]);
        return 2;
    }
    const char *img = argv[1], *dirname = argv[2];
    /* --allow-linear runs the identical workload against a NON-indexed
     * directory.  It is the control for this test: any accounting drift
     * e2fsck reports in both modes belongs to the shared alloc/free
     * paths, not to index maintenance. */
    int allow_linear = (argc > 3 && strcmp(argv[3], "--allow-linear") == 0);
    const char *env = getenv("HTREE_ADD");
    const int NADD = env ? atoi(env) : NADD_DEFAULT;
    if (NADD < 1) FAIL("HTREE_ADD must be positive\n");

    printf("Testing EXT2 htree directory writes (host-side)...\n");

    FILE *f = fopen(img, "rb");
    if (!f) FAIL("cannot open %s\n", img);
    fseek(f, 0, SEEK_END); disk_len = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    disk = malloc(disk_len);
    if (!disk || fread(disk, 1, disk_len, f) != disk_len) FAIL("cannot read %s\n", img);
    fclose(f);

    fs_node_t dev;
    memset(&dev, 0, sizeof(dev));
    dev.read  = mock_read;
    dev.write = mock_write;

    fs_node_t *root = ext2_mount("test", 0, &dev);
    if (!root) FAIL("ext2_mount returned NULL\n");

    fs_node_t *dir = root->finddir(root, (char *)dirname);
    if (!dir) FAIL("finddir('%s') returned NULL\n", dirname);

    ext2_node_t *dctx = (ext2_node_t *)(uintptr_t)dir->impl;
    if (!(dctx->inode.i_flags & EXT2_INDEX_FL) && !allow_linear)
        FAIL("/%s is not indexed (i_flags=0x%x) — the image is not "
             "exercising the htree path\n", dirname, dctx->inode.i_flags);
    printf("  /%s %s (i_flags=0x%x), i_size=%u\n", dirname,
           (dctx->inode.i_flags & EXT2_INDEX_FL) ? "is indexed" : "is LINEAR (control)",
           dctx->inode.i_flags, dctx->inode.i_size);

    /* The original failure, verbatim: config.status doing mkdir src/.deps. */
    int rc = dir->mkdir(dir, ".deps", 0755);
    if (rc != 0) FAIL("mkdir('.deps') in an indexed dir returned %d\n", rc);
    printf("  mkdir('.deps') OK\n");

    /* Force leaf splits and index growth. */
    char nm[64];
    for (int i = 0; i < NADD; i++) {
        snprintf(nm, sizeof(nm), "htree_add_%05d", i);
        rc = dir->mknod(dir, nm, 0100644, 0);
        if (rc != 0) FAIL("mknod('%s') returned %d after %d inserts\n", nm, rc, i);
    }
    printf("  inserted %d entries, i_size now %u (%u blocks)\n",
           NADD, dctx->inode.i_size, dctx->inode.i_size / 1024);

    /* Every name must come back through the index. */
    for (int i = 0; i < NADD; i++) {
        snprintf(nm, sizeof(nm), "htree_add_%05d", i);
        fs_node_t *n = dir->finddir(dir, nm);
        if (!n) FAIL("finddir('%s') missed after insert\n", nm);
    }
    if (!dir->finddir(dir, ".deps")) FAIL("finddir('.deps') missed\n");
    printf("  all %d names resolve through the index\n", NADD + 1);

    /* A duplicate must still be refused. */
    rc = dir->mknod(dir, "htree_add_00000", 0100644, 0);
    if (rc != -EEXIST) FAIL("duplicate mknod returned %d, expected -EEXIST\n", rc);

    /* Deletes, then confirm they are really gone. */
    for (int i = 0; i < NADD; i += 3) {
        snprintf(nm, sizeof(nm), "htree_add_%05d", i);
        rc = dir->unlink(dir, nm);
        if (rc != 0) FAIL("unlink('%s') returned %d\n", nm, rc);
    }
    for (int i = 0; i < NADD; i += 3) {
        snprintf(nm, sizeof(nm), "htree_add_%05d", i);
        if (dir->finddir(dir, nm)) FAIL("finddir('%s') still hits after unlink\n", nm);
    }
    printf("  deleted %d entries and they no longer resolve\n", (NADD + 2) / 3);

    /* Rename within the indexed directory. */
    rc = dir->rename(dir, "htree_add_00001", dir, "htree_renamed");
    if (rc != 0) FAIL("rename in an indexed dir returned %d\n", rc);
    if (dir->finddir(dir, "htree_add_00001")) FAIL("old rename name still resolves\n");
    if (!dir->finddir(dir, "htree_renamed"))  FAIL("renamed entry does not resolve\n");
    printf("  rename OK\n");

    /* Publish the cached free counts and group descriptors before the
     * image is handed to e2fsck.  The allocators keep these in memory
     * and ext2_sync_meta() is what writes them out; skipping it leaves
     * Pass 5 counter drift that has nothing to do with the change under
     * test. */
    rc = root->syncfs(root);
    if (rc != 0) FAIL("syncfs returned %d\n", rc);

    f = fopen(img, "r+b");
    if (!f) FAIL("cannot reopen %s for write\n", img);
    if (fwrite(disk, 1, disk_len, f) != disk_len) FAIL("short write to %s\n", img);
    fclose(f);

    printf("SUCCESS: ext2 htree write test passed.\n");
    return 0;
}
