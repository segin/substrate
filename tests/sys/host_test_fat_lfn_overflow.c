/*
 * host_test_fat_lfn_overflow - fat_parse_lfn() must not write past the
 * caller's buffer for a hostile LFN sequence number.
 *
 * The order field is 6 bits, so a crafted entry can claim order 63, which
 * places its 13 characters at offset (63 - 1) * 13 = 806 -- far beyond the
 * 256-byte name buffers every caller passes.  The parser has to refuse it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include <vfs/vfs.h>

void kprint(const char *s) {
    (void)s;
}

void *kmalloc(size_t size) {
    return calloc(1, size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

#include "../../sys/fs/fat/fat.c"
#include "fat_host_stubs.h"

size_t blkdev_read_bytes(blkdev_t *dev, uint64_t offset, size_t size, void *buffer) {
    (void)dev; (void)offset; (void)size; (void)buffer;
    return 0;
}

static void fill_lfn(fat_lfn_t *lfn, uint8_t order, char c) {
    memset(lfn, 0, sizeof(*lfn));
    lfn->order = order;
    for (int i = 0; i < 5; i++) lfn->name1[i] = (uint8_t)c;
    for (int i = 0; i < 6; i++) lfn->name2[i] = (uint8_t)c;
    for (int i = 0; i < 2; i++) lfn->name3[i] = (uint8_t)c;
}

int main(void) {
    /* Larger than any real caller's buffer, so a write at the bogus
     * offset lands somewhere we can see instead of corrupting the stack. */
    static char big_buffer[2048];
    fat_lfn_t lfn;
    int failed = 0;
    int idx;

    printf("Running FAT LFN Overflow Test...\n");

    /* Last-entry flag | order 63: index (63 - 1) * 13 = 806. */
    fill_lfn(&lfn, 0x40 | 63, 'B');
    memset(big_buffer, 0, sizeof(big_buffer));
    idx = fat_parse_lfn(&lfn, big_buffer, 255);   /* callers pass 256 - 1 */
    if (idx != -1) {
        printf("FAIL: order 63 returned %d, expected -1\n", idx);
        failed++;
    }
    for (size_t i = 0; i < sizeof(big_buffer); i++) {
        if (big_buffer[i] != 0) {
            printf("FAIL: order 63 wrote to index %zu\n", i);
            failed++;
            break;
        }
    }

    /* Normal case: the first entry fills indices 0..12. */
    fill_lfn(&lfn, 1, 'A');
    memset(big_buffer, 0, sizeof(big_buffer));
    idx = fat_parse_lfn(&lfn, big_buffer, 255);
    if (idx != 13 || memcmp(big_buffer, "AAAAAAAAAAAAA", 13) != 0 ||
        big_buffer[13] != 0) {
        printf("FAIL: order 1 returned %d (expected 13)\n", idx);
        failed++;
    }

    if (failed) {
        printf("host_test_fat_lfn_overflow: %d FAILED\n", failed);
        return 1;
    }
    printf("host_test_fat_lfn_overflow: PASS\n");
    return 0;
}
