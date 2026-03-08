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

static uint8_t mock_device_data[4096];
static size_t mock_device_size = 0;
static int mock_device_read_fails = 0;

static size_t mock_read(struct fs_node *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    if (mock_device_read_fails) {
        return 0;
    }
    if ((size_t)offset >= mock_device_size) {
        return 0;
    }
    if ((size_t)offset + size > mock_device_size) {
        size = mock_device_size - (size_t)offset;
    }
    memcpy(buffer, mock_device_data + offset, size);
    return size;
}

static fat_fs_t *create_mock_fs(int fat_type, uint32_t total_clusters, uint8_t *fat_table, uint32_t fat_table_size) {
    fat_fs_t *fs = malloc(sizeof(fat_fs_t));
    memset(fs, 0, sizeof(fat_fs_t));
    fs->fat_type = fat_type;
    fs->total_clusters = total_clusters;
    fs->fat_table = fat_table;
    fs->fat_table_size = fat_table_size;
    return fs;
}

static void test_fat32_in_memory(void) {
    uint32_t fat32_table[10];
    memset(fat32_table, 0, sizeof(fat32_table));
    fat32_table[2] = 0x00000003;
    fat32_table[3] = 0x0FFFFFF8;
    fat32_table[5] = 0x1FFFFFFF;
    fat32_table[6] = 0x0FFFFFF7;

    fat_fs_t *fs = create_mock_fs(32, 10, (uint8_t *)fat32_table, sizeof(fat32_table));
    assert(fat_get_next_cluster(fs, 2) == 3);
    assert(fat_get_next_cluster(fs, 3) == 0x0FFFFFFF);
    assert(fat_get_next_cluster(fs, 5) == 0x0FFFFFFF);
    assert(fat_get_next_cluster(fs, 6) == 0x0FFFFFF7);
    free(fs);
}

static void test_fat16_in_memory(void) {
    uint16_t fat16_table[10];
    memset(fat16_table, 0, sizeof(fat16_table));
    fat16_table[2] = 0x0003;
    fat16_table[3] = 0xFFF8;

    fat_fs_t *fs = create_mock_fs(16, 10, (uint8_t *)fat16_table, sizeof(fat16_table));
    assert(fat_get_next_cluster(fs, 2) == 3);
    assert(fat_get_next_cluster(fs, 3) == 0x0FFFFFFF);
    free(fs);
}

static void test_fat12_in_memory(void) {
    uint8_t fat12_table[6];
    memset(fat12_table, 0, sizeof(fat12_table));
    fat12_table[3] = 0x03;
    fat12_table[4] = 0x80;
    fat12_table[5] = 0xFF;

    fat_fs_t *fs = create_mock_fs(12, 10, fat12_table, sizeof(fat12_table));
    assert(fat_get_next_cluster(fs, 2) == 3);
    assert(fat_get_next_cluster(fs, 3) == 0x0FFFFFFF);
    free(fs);
}

static void test_null_fat_table(void) {
    fat_fs_t *fs = create_mock_fs(32, 10, NULL, 0);
    assert(fat_get_next_cluster(fs, 2) == 0x0FFFFFFF);
    free(fs);
}

static void test_invalid_fat_type(void) {
    uint8_t dummy_table[10];
    fat_fs_t *fs = create_mock_fs(99, 10, dummy_table, sizeof(dummy_table));
    assert(fat_get_next_cluster(fs, 2) == 0x0FFFFFFF);
    free(fs);
}

static void test_fat32_device_read(void) {
    fat_fs_t *fs = create_mock_fs(32, 10, NULL, 0);

    fs_node_t mock_node;
    memset(&mock_node, 0, sizeof(mock_node));
    mock_node.read = mock_read;
    fs->device = &mock_node;

    fs->fat_start_sector = 1;
    fs->bpb.bytes_per_sector = 512;

    // Set up mock device data
    mock_device_size = 4096;
    mock_device_read_fails = 0;
    memset(mock_device_data, 0, mock_device_size);

    // offset = fat_start_sector * bytes_per_sector + cluster * 4
    // offset for cluster 2 = 1 * 512 + 2 * 4 = 512 + 8 = 520
    uint32_t val2 = 0x00000003;
    memcpy(mock_device_data + 520, &val2, sizeof(val2));

    // offset for cluster 3 = 1 * 512 + 3 * 4 = 512 + 12 = 524
    uint32_t val3 = 0x0FFFFFF8;
    memcpy(mock_device_data + 524, &val3, sizeof(val3));

    assert(fat_get_next_cluster(fs, 2) == 3);
    assert(fat_get_next_cluster(fs, 3) == 0x0FFFFFFF);
    free(fs);
}

static void test_fat16_device_read(void) {
    fat_fs_t *fs = create_mock_fs(16, 10, NULL, 0);

    fs_node_t mock_node;
    memset(&mock_node, 0, sizeof(mock_node));
    mock_node.read = mock_read;
    fs->device = &mock_node;

    fs->fat_start_sector = 1;
    fs->bpb.bytes_per_sector = 512;

    mock_device_size = 4096;
    mock_device_read_fails = 0;
    memset(mock_device_data, 0, mock_device_size);

    // offset = fat_start_sector * bytes_per_sector + cluster * 2
    // offset for cluster 2 = 1 * 512 + 2 * 2 = 512 + 4 = 516
    uint16_t val2 = 0x0003;
    memcpy(mock_device_data + 516, &val2, sizeof(val2));

    // offset for cluster 3 = 1 * 512 + 3 * 2 = 512 + 6 = 518
    uint16_t val3 = 0xFFF8;
    memcpy(mock_device_data + 518, &val3, sizeof(val3));

    assert(fat_get_next_cluster(fs, 2) == 3);
    assert(fat_get_next_cluster(fs, 3) == 0x0FFFFFFF);
    free(fs);
}

static void test_fat12_device_read(void) {
    fat_fs_t *fs = create_mock_fs(12, 10, NULL, 0);

    fs_node_t mock_node;
    memset(&mock_node, 0, sizeof(mock_node));
    mock_node.read = mock_read;
    fs->device = &mock_node;

    fs->fat_start_sector = 1;
    fs->bpb.bytes_per_sector = 512;

    mock_device_size = 4096;
    mock_device_read_fails = 0;
    memset(mock_device_data, 0, mock_device_size);

    // offset = fat_start_sector * bytes_per_sector + cluster + (cluster / 2)
    // offset for cluster 2 = 1 * 512 + 2 + 1 = 515
    // offset for cluster 3 = 1 * 512 + 3 + 1 = 516

    // Cluster 2 value: 0x003. Cluster 3 value: EOC (0xFFF).
    // In FAT12, clusters are 1.5 bytes.
    // 0: ?, 1: ?, 2: byte 0 of cluster 2, 3: nibble of cluster 2 and nibble of cluster 3, 4: byte of cluster 3
    // Offset for cluster 2 is 512 + 3 = 515
    // Offset for cluster 3 is 512 + 4 = 516

    // Let's just set the bytes.
    // Offset 3 (byte 515): 0x03 (lower 8 bits of 0x003)
    // Offset 4 (byte 516): 0xF0 (upper 4 bits of 0x003 -> 0, lower 4 bits of 0xFFF -> F)
    // Offset 5 (byte 517): 0xFF (upper 8 bits of 0xFFF)
    mock_device_data[515] = 0x03;
    mock_device_data[516] = 0xF0;
    mock_device_data[517] = 0xFF;

    assert(fat_get_next_cluster(fs, 2) == 3);
    assert(fat_get_next_cluster(fs, 3) == 0x0FFFFFFF);
    free(fs);
}

static void test_device_read_error(void) {
    fat_fs_t *fs = create_mock_fs(32, 10, NULL, 0);

    fs_node_t mock_node;
    memset(&mock_node, 0, sizeof(mock_node));
    mock_node.read = mock_read;
    fs->device = &mock_node;

    fs->fat_start_sector = 1;
    fs->bpb.bytes_per_sector = 512;

    mock_device_size = 4096;
    mock_device_read_fails = 1;

    assert(fat_get_next_cluster(fs, 2) == 0x0FFFFFFF);
    free(fs);
}

int main(void) {
    test_null_fat_table();
    test_invalid_fat_type();
    test_fat32_in_memory();
    test_fat16_in_memory();
    test_fat12_in_memory();
    test_fat32_device_read();
    test_fat16_device_read();
    test_fat12_device_read();
    test_device_read_error();
    puts("PASS: host_test_fat_get_next_cluster");
    return 0;
}
