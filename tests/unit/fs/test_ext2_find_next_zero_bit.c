#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef HOST_TEST
#define HOST_TEST
#endif

#include <sys/types.h>
#include <vfs/vfs.h>

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

#include <sys/lock.h>
void mutex_init(mutex_t *m, const char *name) { (void)m; (void)name; }
void mutex_lock(mutex_t *m) { (void)m; }
void mutex_unlock(mutex_t *m) { (void)m; }

void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

int64_t get_time(void) {
    return 0;
}

#include <vm/uma.h>
uma_zone_t *uma_zcreate(const char *name, size_t size, uma_ctor ctor, uma_dtor dtor, uma_init init, uma_fini fini, int align, uint32_t flags) {
    (void)ctor; (void)dtor; (void)init; (void)fini; (void)align; (void)flags;
    uma_zone_t *zone = (uma_zone_t *)calloc(1, sizeof(uma_zone_t));
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

#define vasprintf kernel_vasprintf
#include <fs/ext2/ext2.c>

static void run_ext2_find_next_zero_bit_test(void) {
    uint32_t found_idx;
    uint8_t bitmap[32];

    memset(bitmap, 0, sizeof(bitmap));
    if (!ext2_find_next_zero_bit(bitmap, 256, 0, 256, &found_idx) || found_idx != 0) exit(1);

    memset(bitmap, 0, sizeof(bitmap));
    bitmap[0] = 0x01;
    if (!ext2_find_next_zero_bit(bitmap, 256, 0, 256, &found_idx) || found_idx != 1) exit(1);

    memset(bitmap, 0, sizeof(bitmap));
    bitmap[0] = 0xFF;
    bitmap[1] = 0xFF;
    bitmap[2] = 0x01;
    if (!ext2_find_next_zero_bit(bitmap, 256, 0, 256, &found_idx) || found_idx != 17) exit(1);

    memset(bitmap, 0, sizeof(bitmap));
    if (!ext2_find_next_zero_bit(bitmap, 256, 10, 256, &found_idx) || found_idx != 10) exit(1);

    memset(bitmap, 0xFF, sizeof(bitmap));
    if (ext2_find_next_zero_bit(bitmap, 256, 0, 256, &found_idx)) exit(1);

    memset(bitmap, 0xFF, sizeof(bitmap));
    bitmap[15] &= (uint8_t)~0x01;
    if (!ext2_find_next_zero_bit(bitmap, 256, 0, 256, &found_idx) || found_idx != 120) exit(1);

    memset(bitmap, 0, sizeof(bitmap));
    bitmap[0] = 0xFF;
    if (ext2_find_next_zero_bit(bitmap, 256, 0, 4, &found_idx)) exit(1);

    memset(bitmap, 0xFF, sizeof(bitmap));
    bitmap[4] &= (uint8_t)~0x80;
    if (!ext2_find_next_zero_bit(bitmap, 256, 33, 256, &found_idx) || found_idx != 39) exit(1);

    memset(bitmap, 0, sizeof(bitmap));
    if (ext2_find_next_zero_bit(bitmap, 256, 10, 5, &found_idx)) exit(1);

    memset(bitmap, 0xFF, sizeof(bitmap));
    bitmap[15] &= (uint8_t)~0x80;
    if (!ext2_find_next_zero_bit(bitmap, 256, 0, 128, &found_idx) || found_idx != 127) exit(1);

    memset(bitmap, 0xFF, sizeof(bitmap));
    bitmap[1] &= (uint8_t)~0x04;
    if (!ext2_find_next_zero_bit(bitmap, 256, 5, 256, &found_idx) || found_idx != 10) exit(1);

    memset(bitmap, 0xFF, sizeof(bitmap));
    bitmap[10] = 0;
    if (ext2_find_next_zero_bit(bitmap, 64, 0, 128, &found_idx)) exit(1);
}

int main(void) {
    run_ext2_find_next_zero_bit_test();
    puts("PASS: test_ext2_find_next_zero_bit");
    return 0;
}
