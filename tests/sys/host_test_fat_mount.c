#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vfs/vfs.h>

void kprint(const char *str) {
    (void)str;
}

/*
 * Each allocation is exactly its size, with a canary after it that kfree()
 * checks: fat_mount() read the whole 512-byte boot sector into fs->bpb, 476
 * bytes past it, over the rest of fat_fs_t and its neighbours in the
 * kernel's small-size slab.  (This stub used to round every allocation up
 * to a page, which hid that.)
 */
#define CANARY_LEN  1024
#define CANARY_BYTE 0xA5

void *kmalloc(size_t size) {
    unsigned char *p = malloc(sizeof(size_t) + size + CANARY_LEN);

    if (!p)
        return NULL;
    memcpy(p, &size, sizeof(size));
    memset(p + sizeof(size_t) + size, CANARY_BYTE, CANARY_LEN);
    return p + sizeof(size_t);
}

void kfree(void *ptr, size_t size) {
    unsigned char *p = (unsigned char *)ptr - sizeof(size_t);
    size_t real;

    (void)size;
    if (!ptr)
        return;
    memcpy(&real, p, sizeof(real));
    for (size_t i = 0; i < CANARY_LEN; i++) {
        if (p[sizeof(size_t) + real + i] != CANARY_BYTE) {
            fprintf(stderr, "heap overflow past a %zu-byte allocation\n", real);
            abort();
        }
    }
    free(p);
}

void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

static uint8_t mock_disk[512 * 6000];

static size_t mock_device_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    if (offset < 0) {
        return 0;
    }
    if ((size_t)offset + size > sizeof(mock_disk)) {
        return 0;
    }
    memcpy(buffer, mock_disk + offset, size);
    return size;
}

#include "../../sys/fs/fat/fat.c"
#include "fat_host_stubs.h"

size_t blkdev_read_bytes(blkdev_t *dev, uint64_t offset, size_t size, void *buffer) {
    (void)dev; (void)offset; (void)size; (void)buffer;
    return 0;
}

static size_t mock_device_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node;
    if (offset < 0 || (size_t)offset + size > sizeof(mock_disk)) {
        return 0;
    }
    memcpy(mock_disk + offset, buffer, size);
    return size;
}

static fs_node_t mock_device = {
    .read = mock_device_read,
    .write = mock_device_write,
};

static void reset_mock_disk(void) {
    /*
     * The FAT driver no longer keeps a single static root node/context;
     * fat_alloc_node() overwrites a slot in fat_fs_node_cache[]/fat_node_cache[]
     * on every mount, so there is nothing test-side to reset here.
     */
    memset(mock_disk, 0, sizeof(mock_disk));
}

static void write_mock_fat16_boot_sector(void) {
    fat_bpb_t *bpb = (fat_bpb_t *)mock_disk;

    memset(bpb, 0, sizeof(*bpb));
    bpb->jmp[0] = 0xEB;
    bpb->jmp[1] = 0x3C;
    bpb->jmp[2] = 0x90;
    memcpy(bpb->oem, "mkfs.fat", 8);
    bpb->bytes_per_sector = 512;
    bpb->sectors_per_cluster = 1;
    bpb->reserved_sectors = 1;
    bpb->fat_count = 2;
    bpb->root_entries = 512;
    bpb->total_sectors_16 = 5000;
    bpb->media_type = 0xF8;
    bpb->fat_size_16 = 9;
    bpb->sectors_per_track = 32;
    bpb->head_count = 8;
    bpb->hidden_sectors = 0;
    bpb->total_sectors_32 = 0;

    mock_disk[510] = 0x55;
    mock_disk[511] = 0xAA;
}

static void test_fat_mount_rejects_zeroed_bpb(void) {
    reset_mock_disk();
    assert(fat_mount(NULL, 0, &mock_device) == NULL);
}

static void test_fat_mount_accepts_minimal_fat16(void) {
    fs_node_t *root;

    reset_mock_disk();
    write_mock_fat16_boot_sector();

    root = fat_mount(NULL, 0, &mock_device);
    assert(root != NULL);
    assert(root->flags == FS_DIRECTORY);
    assert(strcmp(root->name, "/") == 0);

    /*
     * fat_mount() returns a dedicated root node (copied out of the node
     * ring cache so later lookups cannot overwrite it) whose impl points
     * at its fat_node_t.  The test is built -m32, so impl holds the whole
     * pointer.  fs->root_node must point back at the returned root node.
     */
    fat_node_t *root_ctx = (fat_node_t *)(uintptr_t)root->impl;
    assert(root_ctx != NULL);
    fat_fs_t *fs = root_ctx->fs;
    assert(fs != NULL);
    assert(fs->root_node == root);
    assert(fs->fat_type == 16);
    assert(fs->bpb.bytes_per_sector == 512);
    assert(fs->cluster_size == 512);
    assert(fs->root_dir_first_sector == 19);
    assert(fs->first_data_sector == 51);

    /* The driver's own teardown frees the FAT cache, fs and root node. */
    assert(root->unmount != NULL);
    assert(root->unmount(root) == 0);
}

/*
 * A file unlinked while open keeps its clusters until its last close: they
 * were freed at once, the next file written took them, and the open file
 * read that file's data (and would have written over it).
 */
static void test_fat_unlink_open_file_keeps_clusters(void) {
    fs_node_t *root;
    fs_node_t *a;
    fs_node_t *b;
    uint8_t data[2048];
    uint8_t back[2048];

    reset_mock_disk();
    write_mock_fat16_boot_sector();
    root = fat_mount(NULL, 0, &mock_device);
    assert(root != NULL);

    assert(root->mknod(root, "A.TXT", 0100644, 0) == 0);
    a = root->finddir(root, "A.TXT");
    assert(a != NULL);
    a->open(a);                     /* as an open descriptor holds it */
    a->close(a);                    /* finddir's lookup reference */
    memset(data, 'A', sizeof(data));
    assert(a->write(a, 0, sizeof(data), data) == sizeof(data));

    assert(root->unlink(root, "A.TXT") == 0);
    assert(root->finddir(root, "A.TXT") == NULL);

    assert(root->mknod(root, "B.TXT", 0100644, 0) == 0);
    b = root->finddir(root, "B.TXT");
    assert(b != NULL);
    memset(data, 'B', sizeof(data));
    assert(b->write(b, 0, sizeof(data), data) == sizeof(data));

    memset(back, 0, sizeof(back));
    assert(a->read(a, 0, sizeof(back), back) == sizeof(back));
    for (size_t i = 0; i < sizeof(back); i++) {
        assert(back[i] == 'A');
    }

    a->close(a);                    /* the last close: its clusters go */
    memset(back, 0, sizeof(back));
    assert(b->read(b, 0, sizeof(back), back) == sizeof(back));
    for (size_t i = 0; i < sizeof(back); i++) {
        assert(back[i] == 'B');
    }
    b->close(b);
    assert(root->unmount(root) == 0);
}

int main(void) {
    test_fat_mount_rejects_zeroed_bpb();
    test_fat_mount_accepts_minimal_fat16();
    test_fat_unlink_open_file_keeps_clusters();
    puts("PASS: host_test_fat_mount");
    return 0;
}
