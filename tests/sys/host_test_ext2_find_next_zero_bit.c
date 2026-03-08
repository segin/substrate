#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef long off_t;

void kprint(const char *str) {
    (void)str;
}

#include <vm/vm_kmem.h>
void *kmalloc(size_t size) {
    return calloc(1, size);
}
void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

#include <vfs/vfs.h>
#include <sys/lock.h>

void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

void mutex_lock(mutex_t *m) { (void)m; }
void mutex_unlock(mutex_t *m) { (void)m; }
void mutex_init(mutex_t *m, const char *name) { (void)m; (void)name; }

#include <vm/uma.h>
void *uma_zalloc(uma_zone_t *zone, int flags) { (void)zone; (void)flags; return calloc(1, 4096); }
void uma_zfree(uma_zone_t *zone, void *item) { (void)zone; free(item); }
uma_zone_t *uma_zcreate(
    const char *name, size_t size,
    int (*ctor)(void *mem, int size, void *arg, int flags),
    void (*dtor)(void *mem, int size, void *arg),
    int (*init)(void *mem, int size, int flags),
    void (*fini)(void *mem, int size),
    int align, uint32_t flags)
{
    (void)name; (void)size; (void)ctor; (void)dtor; (void)init; (void)fini; (void)align; (void)flags;
    return NULL;
}

int64_t get_time(void) {
    return 0;
}

size_t mock_device_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    return 0;
}

size_t mock_device_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    return 0;
}

#define vasprintf kernel_vasprintf
#include "../../sys/fs/ext2/ext2.c"

int main(void) {
    printf("Running ext2_find_next_zero_bit Tests...\n");
    int failed = 0;
    uint32_t found_idx;
    int ret;
    uint32_t bitmap[16]; // 512 bits

    // Test 1: Empty bitmap (all 0s)
    memset(bitmap, 0, sizeof(bitmap));
    ret = ext2_find_next_zero_bit(bitmap, 512, 0, 512, &found_idx);
    if (!(ret == 1 && found_idx == 0)) {
        printf("FAILED Test 1: Expected return 1 and index 0, got %d and %u\n", ret, found_idx);
        failed++;
    }

    // Test 2: Full bitmap (all 1s)
    memset(bitmap, 0xFF, sizeof(bitmap));
    ret = ext2_find_next_zero_bit(bitmap, 512, 0, 512, &found_idx);
    if (!(ret == 0)) {
        printf("FAILED Test 2: Expected return 0 for full bitmap, got %d\n", ret);
        failed++;
    }

    // Test 3: First bit zero
    memset(bitmap, 0xFF, sizeof(bitmap));
    ((uint8_t*)bitmap)[0] &= ~1; // Set bit 0 to 0
    ret = ext2_find_next_zero_bit(bitmap, 512, 0, 512, &found_idx);
    if (!(ret == 1 && found_idx == 0)) {
        printf("FAILED Test 3: Expected return 1 and index 0, got %d and %u\n", ret, found_idx);
        failed++;
    }

    // Test 4: Last bit zero
    memset(bitmap, 0xFF, sizeof(bitmap));
    ((uint8_t*)bitmap)[63] &= ~(1 << 7); // Set bit 511 to 0
    ret = ext2_find_next_zero_bit(bitmap, 512, 0, 512, &found_idx);
    if (!(ret == 1 && found_idx == 511)) {
        printf("FAILED Test 4: Expected return 1 and index 511, got %d and %u\n", ret, found_idx);
        failed++;
    }

    // Test 5: Unaligned start index
    memset(bitmap, 0xFF, sizeof(bitmap));
    ((uint8_t*)bitmap)[1] &= ~(1 << 3); // Set bit 11 to 0
    ret = ext2_find_next_zero_bit(bitmap, 512, 5, 512, &found_idx);
    if (!(ret == 1 && found_idx == 11)) {
        printf("FAILED Test 5: Expected return 1 and index 11, got %d and %u\n", ret, found_idx);
        failed++;
    }

    // Test 6: Fast path skip full words
    memset(bitmap, 0xFF, sizeof(bitmap));
    bitmap[5] = 0xFFFF0FFF; // 0xFFFF0FFF in binary means bits 12-15 of word 5 are zero
    // Word 5 starts at bit 160. Bits 12-15 are 172-175
    ret = ext2_find_next_zero_bit(bitmap, 512, 0, 512, &found_idx);
    if (!(ret == 1 && found_idx == 172)) {
        printf("FAILED Test 6: Expected return 1 and index 172, got %d and %u\n", ret, found_idx);
        failed++;
    }

    // Test 7: Start past the first zero bit
    memset(bitmap, 0xFF, sizeof(bitmap));
    ((uint8_t*)bitmap)[0] &= ~1; // bit 0 is zero
    ((uint8_t*)bitmap)[1] &= ~(1 << 3); // bit 11 is zero
    ret = ext2_find_next_zero_bit(bitmap, 512, 5, 512, &found_idx);
    if (!(ret == 1 && found_idx == 11)) {
        printf("FAILED Test 7: Expected return 1 and index 11, got %d and %u\n", ret, found_idx);
        failed++;
    }

    // Test 8: Range end bound limits search
    memset(bitmap, 0xFF, sizeof(bitmap));
    ((uint8_t*)bitmap)[2] &= ~1; // bit 16 is zero
    ret = ext2_find_next_zero_bit(bitmap, 512, 0, 16, &found_idx); // up to index 15
    if (!(ret == 0)) {
        printf("FAILED Test 8: Expected return 0, got %d\n", ret);
        failed++;
    }

    // Test 9: Fast path end bound limitation
    memset(bitmap, 0xFF, sizeof(bitmap));
    bitmap[2] = 0x7FFFFFFF; // bit 95 is zero (highest bit of word 2)
    ret = ext2_find_next_zero_bit(bitmap, 512, 0, 95, &found_idx); // up to index 94
    if (!(ret == 0)) {
        printf("FAILED Test 9: Expected return 0, got %d\n", ret);
        failed++;
    }

    // Test 10: Search after end > total_bits
    memset(bitmap, 0xFF, sizeof(bitmap));
    ((uint8_t*)bitmap)[63] &= ~(1 << 7); // bit 511 is zero
    ret = ext2_find_next_zero_bit(bitmap, 500, 0, 600, &found_idx);
    if (!(ret == 0)) {
        printf("FAILED Test 10: Expected return 0, got %d\n", ret);
        failed++;
    }

    if (failed != 0) {
        printf("%d tests FAILED!\n", failed);
        return 1;
    }

    printf("All tests PASSED!\n");
    return 0;
}