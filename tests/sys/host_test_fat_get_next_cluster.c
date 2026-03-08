#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>

#include <vfs/vfs.h>

// Mocks
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

#include "../../sys/fs/fat/fat.c"

// Helper to construct a mock fat_fs_t
fat_fs_t *create_mock_fs(int fat_type, uint32_t total_clusters, uint8_t *fat_table, uint32_t fat_table_size) {
    fat_fs_t *fs = malloc(sizeof(fat_fs_t));
    memset(fs, 0, sizeof(fat_fs_t));
    fs->fat_type = fat_type;
    fs->total_clusters = total_clusters;
    fs->fat_table = fat_table;
    fs->fat_table_size = fat_table_size;
    return fs;
}

void test_fat32_in_memory() {
    printf("Running FAT32 tests...\n");
    uint32_t fat32_table[10];
    memset(fat32_table, 0, sizeof(fat32_table));

    // Valid clusters
    fat32_table[2] = 0x00000003; // next is 3
    fat32_table[3] = 0x0FFFFFF8; // EOC
    fat32_table[4] = 0x00000000; // free
    fat32_table[5] = 0x1FFFFFFF; // masked bits
    fat32_table[6] = 0x0FFFFFF7; // Bad cluster, treated as regular but normally marked bad

    fat_fs_t *fs = create_mock_fs(32, 10, (uint8_t *)fat32_table, sizeof(fat32_table));

    assert(fat_get_next_cluster(fs, 2) == 3);
    assert(fat_get_next_cluster(fs, 3) == 0x0FFFFFFF); // EOC
    assert(fat_get_next_cluster(fs, 5) == 0x0FFFFFFF); // 0x1FFFFFFF masked & 0x0FFFFFFF = 0x0FFFFFFF (EOC)
    assert(fat_get_next_cluster(fs, 6) == 0x0FFFFFF7); // Just passing through

    free(fs);
}

void test_fat16_in_memory() {
    printf("Running FAT16 tests...\n");
    uint16_t fat16_table[10];
    memset(fat16_table, 0, sizeof(fat16_table));

    fat16_table[2] = 0x0003;
    fat16_table[3] = 0xFFF8; // EOC

    fat_fs_t *fs = create_mock_fs(16, 10, (uint8_t *)fat16_table, sizeof(fat16_table));

    assert(fat_get_next_cluster(fs, 2) == 3);
    assert(fat_get_next_cluster(fs, 3) == 0x0FFFFFFF); // EOC maps to 0x0FFFFFFF

    free(fs);
}

void test_fat12_in_memory() {
    printf("Running FAT12 tests...\n");
    // FAT12 uses 12 bits per entry.
    uint8_t fat12_table[6];
    memset(fat12_table, 0, sizeof(fat12_table));

    // Entry 2 (even) -> 0x003
    // Entry 3 (odd) -> 0xFF8 (EOC)
    // Offset for cluster 2: 2 + (2/2) = 3
    fat12_table[3] = 0x03;
    fat12_table[4] = 0x80; // (0x003 >> 8) | ((0xFF8 & 0x0F) << 4) -> 0 | (8 << 4) = 0x80
    fat12_table[5] = 0xFF; // 0xFF8 >> 4 = 0xFF

    fat_fs_t *fs = create_mock_fs(12, 10, fat12_table, sizeof(fat12_table));

    assert(fat_get_next_cluster(fs, 2) == 3);
    assert(fat_get_next_cluster(fs, 3) == 0x0FFFFFFF); // EOC

    free(fs);
}

void test_null_fat_table() {
    printf("Running null fat table tests...\n");
    // According to the prompt's source code, `if (!fs->fat_table) return 0x0FFFFFFF;`
    // We shouldn't test `fs == NULL` because the prompt says the code is:
    // `if (!fs->fat_table) return 0x0FFFFFFF;` -> so `fs == NULL` would segfault.
    fat_fs_t *fs = create_mock_fs(32, 10, NULL, 0);
    assert(fat_get_next_cluster(fs, 2) == 0x0FFFFFFF);
    free(fs);
}

void test_invalid_fat_type() {
    printf("Running invalid FAT type tests...\n");
    uint8_t dummy_table[10];
    fat_fs_t *fs = create_mock_fs(99, 10, dummy_table, sizeof(dummy_table));
    assert(fat_get_next_cluster(fs, 2) == 0x0FFFFFFF);
    free(fs);
}

int main() {
    test_null_fat_table();
    test_invalid_fat_type();
    test_fat32_in_memory();
    test_fat16_in_memory();
    test_fat12_in_memory();
    printf("All fat_get_next_cluster tests passed successfully!\n");
    return 0;
}
