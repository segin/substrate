#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vfs/vfs.h>

void kprint(const char *str) {
    (void)str;
}

void *kmalloc(size_t size) {
    return malloc(size);
}

void kfree(void *ptr) {
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

static fs_node_t mock_device = {
    .read = mock_device_read,
};

static void reset_mock_disk(void) {
    memset(mock_disk, 0, sizeof(mock_disk));
    memset(&fat_root_node, 0, sizeof(fat_root_node));
    memset(&fat_root_ctx, 0, sizeof(fat_root_ctx));
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
    assert(root == &fat_root_node);
    assert(root->flags == FS_DIRECTORY);
    assert(strcmp(root->name, "/") == 0);
    assert(fat_global_fs.fat_type == 16);
    assert(fat_global_fs.bpb.bytes_per_sector == 512);
    assert(fat_global_fs.cluster_size == 512);
    assert(fat_global_fs.root_dir_first_sector == 19);
    assert(fat_global_fs.first_data_sector == 51);
}

int main(void) {
    test_fat_mount_rejects_zeroed_bpb();
    test_fat_mount_accepts_minimal_fat16();
    puts("PASS: host_test_fat_mount");
    return 0;
}
