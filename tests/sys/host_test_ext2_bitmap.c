#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <stdarg.h>

// ==========================================
// Mocks for Kernel Environment
// ==========================================

typedef long off_t;

// Use the kernel's own headers but mock the conflicting parts
#define _SYS_LOCK_H
typedef struct { int dummy; } mutex_t;
void mutex_init(mutex_t *m, const char *name) { (void)m; (void)name; }
void mutex_lock(mutex_t *m) { (void)m; }
void mutex_unlock(mutex_t *m) { (void)m; }

#define _KERN_CONSOLE_H
#define _CONSOLE_H
void kprint(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

#define _VM_UMA_H
typedef struct { int dummy; } uma_zone_t;
uma_zone_t *uma_zcreate(const char *name, size_t size, void *a, void *b, void *c, void *d, int e, int f) {
    (void)name; (void)size; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
    return (uma_zone_t *)0xdeadbeef;
}
void *uma_zalloc(uma_zone_t *zone, int flags) {
    (void)zone; (void)flags;
    return malloc(4096);
}
void uma_zfree(uma_zone_t *zone, void *ptr) {
    (void)zone;
    free(ptr);
}
#define M_WAITOK 0

void *kmalloc(size_t size) {
    return calloc(1, size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

struct filesystem;
typedef struct filesystem filesystem_t;
void vfs_register_filesystem(filesystem_t *fs) { (void)fs; }

int64_t get_time(void) { return 0; }

// We need to define some things that are usually in headers
#define EXT2_NODE_CACHE_SIZE 64
#define EXT2_DCACHE_SIZE 16

// Include the source file
#include "../../sys/fs/ext2/ext2.c"

// ==========================================
// Test Cases
// ==========================================

void test_all_ones() {
    uint32_t bitmap[2] = {0xFFFFFFFF, 0xFFFFFFFF};
    uint32_t found;
    int ret = ext2_find_next_zero_bit(bitmap, 64, 0, 64, &found);
    assert(ret == 0);
    printf("test_all_ones: PASS\n");
}

void test_all_zeros() {
    uint32_t bitmap[2] = {0, 0};
    uint32_t found;
    int ret = ext2_find_next_zero_bit(bitmap, 64, 0, 64, &found);
    assert(ret == 1);
    assert(found == 0);
    printf("test_all_zeros: PASS\n");
}

void test_single_zero_start() {
    uint32_t bitmap[2] = {0xFFFFFFFE, 0xFFFFFFFF};
    uint32_t found;
    int ret = ext2_find_next_zero_bit(bitmap, 64, 0, 64, &found);
    assert(ret == 1);
    assert(found == 0);
    printf("test_single_zero_start: PASS\n");
}

void test_single_zero_middle() {
    // Bit 50 is zero. 50 = 32 + 18.
    // Word 1, bit 18.
    uint32_t bitmap[2] = {0xFFFFFFFF, ~(1 << 18)};
    uint32_t found;
    int ret = ext2_find_next_zero_bit(bitmap, 64, 0, 64, &found);
    assert(ret == 1);
    assert(found == 50);
    printf("test_single_zero_middle: PASS\n");
}

void test_single_zero_end() {
    uint32_t bitmap[2] = {0xFFFFFFFF, 0x7FFFFFFF}; // Bit 63 is 0
    uint32_t found;
    int ret = ext2_find_next_zero_bit(bitmap, 64, 0, 64, &found);
    assert(ret == 1);
    assert(found == 63);
    printf("test_single_zero_end: PASS\n");
}

void test_start_offset() {
    // Bits 0-9 are 1, rest are 0.
    uint32_t bitmap[2] = {0x000003FF, 0x00000000};
    uint32_t found;
    int ret = ext2_find_next_zero_bit(bitmap, 64, 0, 64, &found);
    assert(ret == 1);
    assert(found == 10);

    ret = ext2_find_next_zero_bit(bitmap, 64, 11, 64, &found);
    assert(ret == 1);
    assert(found == 11);
    printf("test_start_offset: PASS\n");
}

void test_unaligned_start() {
    // Bit 35 is 0.
    uint32_t bitmap[2] = {0xFFFFFFFF, ~(1 << 3)};
    uint32_t found;
    // Start at bit 10. It should align to 32, find nothing in 10-31, then use fast path or slow path to find 35.
    int ret = ext2_find_next_zero_bit(bitmap, 64, 10, 64, &found);
    assert(ret == 1);
    assert(found == 35);
    printf("test_unaligned_start: PASS\n");
}

void test_fast_path() {
    uint32_t bitmap[4];
    bitmap[0] = 0xFFFFFFFF;
    bitmap[1] = 0xFFFFFFFF;
    bitmap[2] = 0xFFFFFFFD; // Bit 2*32 + 1 = 65 is 0
    bitmap[3] = 0xFFFFFFFF;
    uint32_t found;
    int ret = ext2_find_next_zero_bit(bitmap, 128, 0, 128, &found);
    assert(ret == 1);
    assert(found == 65);
    printf("test_fast_path: PASS\n");
}

void test_end_boundary() {
    uint32_t bitmap[2] = {0xFFFFFFFF, 0x00000000};
    uint32_t found;
    // Should NOT find a zero bit if we stop before bit 32
    int ret = ext2_find_next_zero_bit(bitmap, 64, 0, 32, &found);
    assert(ret == 0);

    // Should find bit 32 if we allow it
    ret = ext2_find_next_zero_bit(bitmap, 64, 0, 33, &found);
    assert(ret == 1);
    assert(found == 32);
    printf("test_end_boundary: PASS\n");
}

void test_large_bitmap() {
    size_t num_words = 1024; // 32768 bits
    uint32_t *bitmap = malloc(num_words * sizeof(uint32_t));
    memset(bitmap, 0xFF, num_words * sizeof(uint32_t));

    // Set bit 30000 to 0
    uint32_t bit = 30000;
    bitmap[bit / 32] &= ~(1 << (bit % 32));

    uint32_t found;
    int ret = ext2_find_next_zero_bit(bitmap, num_words * 32, 0, num_words * 32, &found);
    assert(ret == 1);
    assert(found == bit);

    free(bitmap);
    printf("test_large_bitmap: PASS\n");
}

int main() {
    printf("Running ext2_find_next_zero_bit tests...\n");
    test_all_ones();
    test_all_zeros();
    test_single_zero_start();
    test_single_zero_middle();
    test_single_zero_end();
    test_start_offset();
    test_unaligned_start();
    test_fast_path();
    test_end_boundary();
    test_large_bitmap();
    printf("All tests passed!\n");
    return 0;
}
