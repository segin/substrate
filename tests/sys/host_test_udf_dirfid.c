/*
 * host_test_udf_dirfid.c — UDF directory entries go where the directory's
 * data is, and the node table keeps nodes that are in use.
 *
 * udf_add_fid used to write the new File Identifier Descriptor at byte 0
 * of the directory's File Entry sector -- over the File Entry itself --
 * rather than into the directory's data; and udf_alloc_node recycled its
 * 64-slot table round robin, so the 65th lookup overwrote a node still in
 * use (the mount's root among them).
 *
 *   1. mkdir-style: create a directory FE, add two FIDs, check the FE
 *      header is intact and both FIDs read back through the directory's
 *      data (udf_read_file), then remove one: it reads back deleted.
 *   2. node table: open one node, allocate 200 more; the open one keeps
 *      its contents.
 */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void kprint(const char *str) { (void)str; }
void *kmalloc(size_t size) { return calloc(1, size); }
void *kzalloc(size_t size) { return calloc(1, size); }
void kfree(void *ptr, size_t size) { (void)size; free(ptr); }

#define HOST_TEST
#include <fs/udf/udf.h>
#include <vfs/vfs.h>
#include <vfs/vnode.h>
#include <sys/lock.h>
#include <drivers/storage/blkdev.h>

void vfs_register_filesystem(filesystem_t *fs) { (void)fs; }
int getnewvnode(const char *tag, struct mount *mp, struct vnodeops *vops,
                struct vnode **vpp) {
    (void)tag; (void)mp; (void)vops; (void)vpp;
    return 0;
}
struct process *current_process = NULL;
size_t blkdev_read_bytes(blkdev_t *dev, uint64_t offset, size_t size,
                         void *buffer) {
    (void)dev; (void)offset; (void)size; (void)buffer;
    return 0;
}
void spinlock_acquire(spinlock_t *lock) { (void)lock; }
void spinlock_release(spinlock_t *lock) { (void)lock; }

#include "../../sys/fs/udf/udf_write.c"
#include "../../sys/fs/udf/udf.c"

#define DISK_SECTORS 64
static uint8_t disk[UDF_SECTOR_SIZE * DISK_SECTORS];

static size_t dread(fs_node_t *n, off_t off, size_t sz, uint8_t *buf) {
    (void)n;
    if (off < 0 || (size_t)off + sz > sizeof(disk)) return 0;
    memcpy(buf, disk + off, sz);
    return sz;
}

static size_t dwrite(fs_node_t *n, off_t off, size_t sz, const uint8_t *buf) {
    (void)n;
    if (off < 0 || (size_t)off + sz > sizeof(disk)) return 0;
    memcpy(disk + off, buf, sz);
    return sz;
}

static fs_node_t dev = { .read = dread, .write = dwrite };

static int fails;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}

/* Is there a live (or, with want_deleted, a deleted) FID named `name` in
 * the directory whose FE is at `block`? */
static int has_fid(struct udf_fs *fs, uint32_t block, const char *name,
                   int want_deleted) {
    uint8_t fe_sec[UDF_SECTOR_SIZE];
    memcpy(fe_sec, disk + (size_t)(fs->partition_start + block) * UDF_SECTOR_SIZE,
           UDF_SECTOR_SIZE);
    struct udf_fe *fe = (struct udf_fe *)fe_sec;
    uint32_t size = (uint32_t)fe->info_length;
    uint8_t *buf = calloc(1, size + 1);
    uint32_t got = udf_read_file(fs, fe, 0, size, buf);
    int found = 0;
    for (uint32_t pos = 0; pos + 38 <= got;) {
        struct udf_fid *fid = (struct udf_fid *)(buf + pos);
        uint32_t len = (38 + fid->impl_use_length + fid->file_id_length + 3) & ~3U;
        const char *nm = (const char *)fid + 38 + fid->impl_use_length + 1;
        if (fid->file_id_length == strlen(name) + 1 &&
            memcmp(nm, name, strlen(name)) == 0) {
            int deleted = (fid->characteristics & UDF_FID_DELETED) != 0;
            found = deleted == want_deleted;
        }
        if (len < 40) break;
        pos += len;
    }
    free(buf);
    return found;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    struct udf_fs *fs = calloc(1, sizeof(*fs));
    fs->device = &dev;
    fs->partition_start = 2;

    /* A space bitmap with blocks 0-3 in use (a set bit is a free block). */
    struct udf_space_bitmap *sbm = (struct udf_space_bitmap *)
        (disk + (size_t)fs->partition_start * UDF_SECTOR_SIZE);
    sbm->tag.tag_id = UDF_TAG_SBD;
    sbm->num_bits = 32;
    sbm->num_bytes = 4;
    uint8_t *bits = (uint8_t *)sbm + sizeof(struct udf_space_bitmap);
    memset(bits, 0xFF, 4);
    bits[0] = 0xF0;
    assert(udf_read_space_bitmap(fs, 0, sizeof(struct udf_space_bitmap) + 4) == 0);

    /* 1: a directory at block 1, then two entries. */
    const uint32_t dir = 1;
    assert(udf_create_fe(fs, dir, UDF_FILETYPE_DIR, 0, 0, 0755) == 0);
    struct udf_long_ad icb = { .length = UDF_SECTOR_SIZE, .block = 9 };
    struct udf_fe dfe;
    memcpy(&dfe, disk + (size_t)(fs->partition_start + dir) * UDF_SECTOR_SIZE,
           sizeof(dfe));
    check(udf_add_fid(fs, &dfe, dir, "alpha", &icb, UDF_FID_DIRECTORY) == 0,
          "add FID alpha");
    check(udf_add_fid(fs, &dfe, dir, "beta", &icb, 0) == 0, "add FID beta");

    struct udf_fe *disk_fe = (struct udf_fe *)
        (disk + (size_t)(fs->partition_start + dir) * UDF_SECTOR_SIZE);
    check(disk_fe->tag.tag_id == UDF_TAG_FE &&
          disk_fe->icb_tag.file_type == UDF_FILETYPE_DIR,
          "the directory's File Entry is still a directory File Entry");
    check(dfe.info_length > 0 && disk_fe->info_length == dfe.info_length,
          "info_length grew and was written");
    check(has_fid(fs, dir, "alpha", 0) && has_fid(fs, dir, "beta", 0),
          "both FIDs read back through the directory's data");

    check(udf_remove_fid(fs, &dfe, dir, "alpha") == 0, "remove alpha");
    check(has_fid(fs, dir, "alpha", 1) && has_fid(fs, dir, "beta", 0),
          "alpha reads back deleted, beta still live");
    check(udf_remove_fid(fs, &dfe, dir, "gamma") == -1, "removing a missing name fails");

    /* 2: an open node is never recycled. */
    uint8_t fe_sec[UDF_SECTOR_SIZE];
    memcpy(fe_sec, disk_fe, UDF_SECTOR_SIZE);
    struct udf_long_ad dicb = { .length = UDF_SECTOR_SIZE, .block = dir };
    fs_node_t *keep = udf_alloc_node(fs, &dicb, fe_sec);
    assert(keep);
    strlcpy(keep->name, "kept", sizeof(keep->name));
    check(keep->open != NULL && keep->close != NULL,
          "nodes have open/close hooks to pin them");
    if (!keep->open || !keep->close) {
        printf("host_test_udf_dirfid: FAIL\n");
        return 1;
    }
    keep->open(keep);
    int reused = 0;
    for (int i = 0; i < 200; i++) {
        fs_node_t *n = udf_alloc_node(fs, &icb, fe_sec);
        if (n == keep) reused = 1;
        if (n) strlcpy(n->name, "other", sizeof(n->name));
    }
    check(!reused && strcmp(keep->name, "kept") == 0,
          "an open node survives 200 more allocations");
    keep->close(keep);

    printf("host_test_udf_dirfid: %s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}
