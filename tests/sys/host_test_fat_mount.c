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
 * fat_mount() reads a full 512-byte boot sector straight into the
 * 36-byte fat_bpb_t field of the heap-allocated fat_fs_t (fat.c:720,
 * fat_read_sectors(fs, 0, 1, &fs->bpb)).  On-target the UMA slab
 * allocator hands back a rounded-up, page-sized slab so that
 * sector-sized write stays inside the allocation; a bare host malloc()
 * of sizeof(fat_fs_t) (~160 bytes) does not, and the over-read smashes
 * the glibc heap.  Mirror the kernel allocator by rounding every
 * allocation up to at least one sector so the test reflects on-target
 * behaviour without weakening it.
 */
void *kmalloc(size_t size) {
    if (size < 4096)
        size = 4096;
    return malloc(size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
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

size_t blkdev_read_bytes(blkdev_t *dev, uint64_t offset, size_t size, void *buffer) {
    (void)dev; (void)offset; (void)size; (void)buffer;
    return 0;
}

static fs_node_t mock_device = {
    .read = mock_device_read,
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
     * fat_mount() now heap-allocates the fat_fs_t and returns a node from
     * fat_alloc_node(), which lives in the parallel static caches
     * fat_fs_node_cache[]/fat_node_cache[].  node->impl can't be used to
     * recover the fat_node_t on a 64-bit host: fat_alloc_node() stores it
     * as a (uint32_t)-truncated pointer (impl is target-pointer-sized).
     * Recover the per-mount context by the node's cache index instead.
     * fs->root_node must point back at the returned root node.
     */
    size_t idx = (size_t)(root - fat_fs_node_cache);
    assert(idx < FAT_NODE_CACHE_SIZE);
    fat_node_t *root_ctx = &fat_node_cache[idx];
    fat_fs_t *fs = root_ctx->fs;
    assert(fs != NULL);
    assert(fs->root_node == root);
    assert(fs->fat_type == 16);
    assert(fs->bpb.bytes_per_sector == 512);
    assert(fs->cluster_size == 512);
    assert(fs->root_dir_first_sector == 19);
    assert(fs->first_data_sector == 51);

    kfree(fs->fat_table, fs->fat_table_size);
    kfree(fs, sizeof(fat_fs_t));
}

int main(void) {
    test_fat_mount_rejects_zeroed_bpb();
    test_fat_mount_accepts_minimal_fat16();
    puts("PASS: host_test_fat_mount");
    return 0;
}
