#include <assert.h>
#include <stddef.h>
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

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

#include "../../sys/fs/fat/fat.c"

static uint8_t mock_disk[1024 * 1024]; // 1MB mock disk
static int read_fails = 0;

static size_t mock_device_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    if (read_fails) return 0;
    if (offset + size > sizeof(mock_disk)) return 0;
    memcpy(buffer, mock_disk + offset, size);
    return size;
}

static fs_node_t mock_device = {
    .read = mock_device_read
};

static fat_fs_t *create_mock_fs(int fat_type) {
    fat_fs_t *fs = malloc(sizeof(fat_fs_t));
    memset(fs, 0, sizeof(fat_fs_t));
    fs->device = &mock_device;
    fs->fat_type = fat_type;

    fs->bpb.bytes_per_sector = 512;
    fs->bpb.sectors_per_cluster = 2; // 1024 bytes per cluster
    fs->bpb.reserved_sectors = 1;
    fs->bpb.fat_count = 1;

    fs->fat_start_sector = 1;
    fs->fat_sectors = 1;
    fs->first_data_sector = 3;
    fs->total_clusters = 100;

    fs->cluster_size = fs->bpb.bytes_per_sector * fs->bpb.sectors_per_cluster;

    return fs;
}

static void test_fat_file_read_basic(void) {
    read_fails = 0;
    fat_fs_t *fs = create_mock_fs(32);

    uint32_t fat32_table[10];
    memset(fat32_table, 0, sizeof(fat32_table));
    fat32_table[2] = 0x0FFFFFF8; // EOC
    fs->fat_table = (uint8_t *)fat32_table;
    fs->fat_table_size = sizeof(fat32_table);

    fat_node_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.fs = fs;
    ctx.size = 100;
    ctx.first_cluster = 2;

    fs_node_t node;
    memset(&node, 0, sizeof(node));
    node.impl = (uintptr_t)&ctx;

    uint32_t sector = fat_cluster_to_sector(fs, 2);
    memset(mock_disk + (sector * 512), 'A', 100);

    uint8_t buffer[100];
    size_t read = fat_file_read(&node, 0, 100, buffer);
    assert(read == 100);
    for (int i=0; i<100; i++) assert(buffer[i] == 'A');

    free(fs);
}

static void test_fat_file_read_offset(void) {
    read_fails = 0;
    fat_fs_t *fs = create_mock_fs(32);

    uint32_t fat32_table[10];
    memset(fat32_table, 0, sizeof(fat32_table));
    fat32_table[2] = 0x0FFFFFF8; // EOC
    fs->fat_table = (uint8_t *)fat32_table;
    fs->fat_table_size = sizeof(fat32_table);

    fat_node_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.fs = fs;
    ctx.size = 500;
    ctx.first_cluster = 2;

    fs_node_t node;
    memset(&node, 0, sizeof(node));
    node.impl = (uintptr_t)&ctx;

    uint32_t sector = fat_cluster_to_sector(fs, 2);
    memset(mock_disk + (sector * 512), 'B', 500);
    mock_disk[(sector * 512) + 150] = 'C';

    uint8_t buffer[100];
    size_t read = fat_file_read(&node, 150, 100, buffer);
    assert(read == 100);
    assert(buffer[0] == 'C');
    assert(buffer[1] == 'B');

    free(fs);
}

static void test_fat_file_read_multiple_clusters(void) {
    read_fails = 0;
    fat_fs_t *fs = create_mock_fs(32);

    uint32_t fat32_table[10];
    memset(fat32_table, 0, sizeof(fat32_table));
    fat32_table[2] = 3;
    fat32_table[3] = 4;
    fat32_table[4] = 0x0FFFFFF8; // EOC
    fs->fat_table = (uint8_t *)fat32_table;
    fs->fat_table_size = sizeof(fat32_table);

    fat_node_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.fs = fs;
    ctx.size = 2500; // Across 3 clusters (each cluster is 1024 bytes)
    ctx.first_cluster = 2;

    fs_node_t node;
    memset(&node, 0, sizeof(node));
    node.impl = (uintptr_t)&ctx;

    uint32_t s2 = fat_cluster_to_sector(fs, 2);
    uint32_t s3 = fat_cluster_to_sector(fs, 3);
    uint32_t s4 = fat_cluster_to_sector(fs, 4);

    memset(mock_disk + (s2 * 512), 'X', 1024);
    memset(mock_disk + (s3 * 512), 'Y', 1024);
    memset(mock_disk + (s4 * 512), 'Z', 452);

    uint8_t buffer[3000];
    size_t read = fat_file_read(&node, 0, 2500, buffer);
    assert(read == 2500);
    assert(buffer[0] == 'X');
    assert(buffer[1023] == 'X');
    assert(buffer[1024] == 'Y');
    assert(buffer[2047] == 'Y');
    assert(buffer[2048] == 'Z');
    assert(buffer[2499] == 'Z');

    // Read starting from an offset inside a cluster, spanning multiple clusters
    memset(buffer, 0, sizeof(buffer));
    read = fat_file_read(&node, 1000, 1200, buffer); // 24 bytes of X, 1024 of Y, 152 of Z
    assert(read == 1200);
    assert(buffer[0] == 'X');
    assert(buffer[23] == 'X');
    assert(buffer[24] == 'Y');
    assert(buffer[1047] == 'Y');
    assert(buffer[1048] == 'Z');
    assert(buffer[1199] == 'Z');

    free(fs);
}

static void test_fat_file_read_eof(void) {
    read_fails = 0;
    fat_fs_t *fs = create_mock_fs(32);

    uint32_t fat32_table[10];
    memset(fat32_table, 0, sizeof(fat32_table));
    fat32_table[2] = 0x0FFFFFF8; // EOC
    fs->fat_table = (uint8_t *)fat32_table;
    fs->fat_table_size = sizeof(fat32_table);

    fat_node_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.fs = fs;
    ctx.size = 500;
    ctx.first_cluster = 2;

    fs_node_t node;
    memset(&node, 0, sizeof(node));
    node.impl = (uintptr_t)&ctx;

    uint8_t buffer[1000];

    // Read fully out of bounds
    size_t read = fat_file_read(&node, 600, 100, buffer);
    assert(read == 0);

    // Read truncated
    read = fat_file_read(&node, 450, 100, buffer);
    assert(read == 50);

    free(fs);
}

static void test_fat_file_read_device_fail(void) {
    read_fails = 1;
    fat_fs_t *fs = create_mock_fs(32);

    uint32_t fat32_table[10];
    memset(fat32_table, 0, sizeof(fat32_table));
    fat32_table[2] = 0x0FFFFFF8; // EOC
    fs->fat_table = (uint8_t *)fat32_table;
    fs->fat_table_size = sizeof(fat32_table);

    fat_node_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.fs = fs;
    ctx.size = 500;
    ctx.first_cluster = 2;

    fs_node_t node;
    memset(&node, 0, sizeof(node));
    node.impl = (uintptr_t)&ctx;

    uint8_t buffer[100];
    size_t read = fat_file_read(&node, 0, 100, buffer);
    assert(read == 0);

    read_fails = 0;
    free(fs);
}

static void test_fat_file_read_corrupted_chain(void) {
    read_fails = 0;
    fat_fs_t *fs = create_mock_fs(32);

    uint32_t fat32_table[10];
    memset(fat32_table, 0, sizeof(fat32_table));
    fat32_table[2] = 0x0FFFFFF8; // EOC - corrupted, should be 3 for size 2000
    fs->fat_table = (uint8_t *)fat32_table;
    fs->fat_table_size = sizeof(fat32_table);

    fat_node_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.fs = fs;
    ctx.size = 2000;
    ctx.first_cluster = 2;

    fs_node_t node;
    memset(&node, 0, sizeof(node));
    node.impl = (uintptr_t)&ctx;

    uint8_t buffer[2000];
    // Offset is 1200, which skips cluster 2 and tries to read cluster 3, but chain ends
    size_t read = fat_file_read(&node, 1200, 100, buffer);
    assert(read == 0);

    // Reading from start but chain ends early
    read = fat_file_read(&node, 0, 2000, buffer);
    assert(read == 1024); // Reads first cluster, then stops

    free(fs);
}

int main(void) {
    test_fat_file_read_basic();
    test_fat_file_read_offset();
    test_fat_file_read_multiple_clusters();
    test_fat_file_read_eof();
    test_fat_file_read_device_fail();
    test_fat_file_read_corrupted_chain();
    puts("PASS: host_test_fat_file_read");
    return 0;
}
