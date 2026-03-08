#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef HOST_TEST
#define HOST_TEST
#endif

// Needed types
#include <sys/types.h>
#include <vfs/vfs.h>

// Mock Kernel Functions
void kprint(const char *msg) {
    (void)msg;
}

void *kmalloc(size_t size) {
    return calloc(1, size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

// Mock Mutex
#include <sys/lock.h>
void mutex_init(mutex_t *m, const char *name) { (void)m; (void)name; }
void mutex_lock(mutex_t *m) { (void)m; }
void mutex_unlock(mutex_t *m) { (void)m; }

// Mock VFS functions
void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

// Mock get_time
int64_t get_time(void) {
    return 0;
}

// UMA Mocks for Ext2
#include <vm/uma.h>

uma_zone_t *uma_zcreate(const char *name, size_t size, uma_ctor ctor, uma_dtor dtor, uma_init init, uma_fini fini, int align, uint32_t flags) {
    (void)ctor; (void)dtor; (void)init; (void)fini; (void)align; (void)flags;
    uma_zone_t *zone = (uma_zone_t *)calloc(1, sizeof(uma_zone_t)); // use calloc to avoid uninit
    if (zone) {
        zone->uz_name = name;
        zone->uz_size = size;
        zone->uz_flags = flags;
    }
    return zone;
}

void *uma_zalloc(uma_zone_t *zone, int flags) {
    (void)flags;
    if (!zone) return NULL;
    return calloc(1, zone->uz_size);
}

void uma_zfree(uma_zone_t *zone, void *item) {
    (void)zone;
    free(item);
}

void uma_zone_set_max(uma_zone_t *zone, int max) { (void)zone; (void)max; }

// Rename colliding kernel function
#define vasprintf kernel_vasprintf

// Include the source under test
// This is relative to tests/unit/fs/
#include <fs/ext2/ext2.c>

// ------------------------------------------------------------------
// Test Logic
// ------------------------------------------------------------------

void run_ext2_find_next_zero_bit_test(void) {
    printf("TEST: ext2_find_next_zero_bit...\n");

    uint32_t found_idx;
    uint8_t bitmap[32]; // 256 bits

    // Test 1: Empty bitmap
    memset(bitmap, 0, sizeof(bitmap));
    if (!ext2_find_next_zero_bit(bitmap, 256, 0, 256, &found_idx) || found_idx != 0) {
        printf("FAIL: Test 1 Empty bitmap failed.\n");
        exit(1);
    }

    // Test 2: First bit used
    memset(bitmap, 0, sizeof(bitmap));
    bitmap[0] = 0x01;
    if (!ext2_find_next_zero_bit(bitmap, 256, 0, 256, &found_idx) || found_idx != 1) {
        printf("FAIL: Test 2 First bit used failed.\n");
        exit(1);
    }

    // Test 3: Multiple bits used
    memset(bitmap, 0, sizeof(bitmap));
    bitmap[0] = 0xFF; // First 8 bits used
    bitmap[1] = 0xFF; // Next 8 bits used
    bitmap[2] = 0x01; // Bit 16 used
    if (!ext2_find_next_zero_bit(bitmap, 256, 0, 256, &found_idx) || found_idx != 17) {
        printf("FAIL: Test 3 Multiple bits used failed.\n");
        exit(1);
    }

    // Test 4: Start offset
    memset(bitmap, 0, sizeof(bitmap));
    if (!ext2_find_next_zero_bit(bitmap, 256, 10, 256, &found_idx) || found_idx != 10) {
        printf("FAIL: Test 4 Start offset failed.\n");
        exit(1);
    }

    // Test 5: Full bitmap
    memset(bitmap, 0xFF, sizeof(bitmap));
    if (ext2_find_next_zero_bit(bitmap, 256, 0, 256, &found_idx)) {
        printf("FAIL: Test 5 Full bitmap failed.\n");
        exit(1);
    }

    // Test 6: Fast path skip
    memset(bitmap, 0xFF, sizeof(bitmap));
    bitmap[15] &= ~0x01; // Leave bit 120 (byte 15, bit 0) as zero. 15 bytes * 8 = 120 bits. Note: bit 0 is cleared.
    if (!ext2_find_next_zero_bit(bitmap, 256, 0, 256, &found_idx) || found_idx != 120) {
        printf("FAIL: Test 6 Fast path skip failed. found_idx=%d\n", found_idx);
        exit(1);
    }

    // Test 7: end truncation
    memset(bitmap, 0, sizeof(bitmap));
    bitmap[0] = 0xFF; // First 8 bits used
    if (ext2_find_next_zero_bit(bitmap, 256, 0, 4, &found_idx)) {
        printf("FAIL: Test 7 end truncation failed. found_idx=%d\n", found_idx);
        exit(1);
    }

    // Test 8: Partial fast path (testing exact bit alignment logic)
    memset(bitmap, 0xFF, sizeof(bitmap));
    bitmap[4] &= ~0x80; // Clear bit 39 (byte 4, bit 7).
    if (!ext2_find_next_zero_bit(bitmap, 256, 33, 256, &found_idx) || found_idx != 39) {
        printf("FAIL: Test 8 partial fast path failed. found_idx=%d\n", found_idx);
        exit(1);
    }

    // Test 9: End bound smaller than start bound (no-op)
    memset(bitmap, 0, sizeof(bitmap));
    if (ext2_find_next_zero_bit(bitmap, 256, 10, 5, &found_idx)) {
        printf("FAIL: Test 9 end bound smaller than start bound failed. found_idx=%d\n", found_idx);
        exit(1);
    }

    // Test 10: Found exactly at end-1 bit
    memset(bitmap, 0xFF, sizeof(bitmap));
    bitmap[15] &= ~0x80; // bit 127
    if (!ext2_find_next_zero_bit(bitmap, 256, 0, 128, &found_idx) || found_idx != 127) {
        printf("FAIL: Test 10 Found exactly at end-1 bit failed. found_idx=%d\n", found_idx);
        exit(1);
    }

    // Test 11: Single 0 bit surrounded by 1s (unaligned start)
    memset(bitmap, 0xFF, sizeof(bitmap));
    bitmap[1] &= ~0x04; // Clear bit 10
    if (!ext2_find_next_zero_bit(bitmap, 256, 5, 256, &found_idx) || found_idx != 10) {
        printf("FAIL: Test 11 single 0 bit surrounded by 1s failed. found_idx=%d\n", found_idx);
        exit(1);
    }

    // Test 12: end > total_bits should be capped
    memset(bitmap, 0xFF, sizeof(bitmap));
    bitmap[10] = 0; // bits 80-87 are 0
    if (!ext2_find_next_zero_bit(bitmap, 64, 0, 128, &found_idx)) { // capped at 64, so should NOT find bit 80
        // Expected to not find anything in 0-64
    } else {
         printf("FAIL: Test 12 end capped to total_bits failed.\n");
         exit(1);
    }

    printf("PASS: ext2_find_next_zero_bit tests passed.\n");
}

int main() {
    run_ext2_find_next_zero_bit_test();
    return 0;
}
