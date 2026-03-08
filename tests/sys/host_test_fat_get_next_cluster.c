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

int main(void) {
    test_null_fat_table();
    test_invalid_fat_type();
    test_fat32_in_memory();
    test_fat16_in_memory();
    test_fat12_in_memory();
    puts("PASS: host_test_fat_get_next_cluster");
    return 0;
}
