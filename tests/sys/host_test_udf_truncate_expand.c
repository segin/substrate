#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

// Mock headers via -I tests/sys/mocks/include
#include <vfs/vfs.h>
#include <kern/console.h>
#include <fs/udf/udf.h>

// Global UDF Context
struct udf_fs udf_ctx;

// Mock CRC functions
uint16_t udf_crc(const uint8_t *data, uint32_t len) {
    (void)data; (void)len;
    return 0;
}

uint8_t udf_tag_checksum(struct udf_tag *tag) {
    uint8_t sum = 0;
    uint8_t *p = (uint8_t *)tag;
    for (int i = 0; i < 4; i++) sum += p[i];
    for (int i = 5; i < 16; i++) sum += p[i];
    return sum;
}

void kprint(const char *fmt) {
    printf("%s", fmt);
}

void *kmalloc(size_t size) {
    return malloc(size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

// Include source (real file)
#include "../../sys/fs/udf/udf_write.c"

// Mocks for test
static uint8_t mock_disk[UDF_SECTOR_SIZE * 1024]; // 512KB mock disk

static size_t mock_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    if (offset + size > sizeof(mock_disk)) return 0;
    memcpy(buffer, mock_disk + offset, size);
    return size;
}

static size_t mock_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node;
    if (offset + size > sizeof(mock_disk)) return 0;
    memcpy(mock_disk + offset, buffer, size);
    return size;
}

static fs_node_t mock_dev = {
    .read = mock_read,
    .write = mock_write,
};

static void dump_ads(struct udf_fe *fe) {
    uint32_t num_ads = fe->alloc_desc_length / sizeof(struct udf_short_ad);
    printf("ADs (num=%u, flags=0x%x): ", num_ads, fe->icb_tag.flags & 0x7);
    struct udf_short_ad *ads = (struct udf_short_ad *)((uint8_t *)fe + sizeof(struct udf_fe));
    for (uint32_t i = 0; i < num_ads; i++) {
        printf("[%u: pos=%u, len=%u] ", i, ads[i].position, ads[i].length & 0x3FFFFFFF);
    }
    printf("\n");
}

int main() {
    printf("Running host_test_udf_truncate_expand...\n");

    memset(mock_disk, 0, sizeof(mock_disk));

    // Setup Space Bitmap at sector 0
    struct udf_space_bitmap *sbm = (struct udf_space_bitmap *)mock_disk;
    sbm->tag.tag_id = UDF_TAG_SBD;
    sbm->num_bits = 1024;
    sbm->num_bytes = 128;

    // Mark sector 0 (SBM), 1 (FE), 2 (Initial data) as allocated
    uint8_t *bitmap_data = mock_disk + sizeof(struct udf_space_bitmap);
    bitmap_data[0] = 0x07;

    // Setup UDF context
    udf_ctx.device = &mock_dev;
    udf_ctx.partition_start = 0;

    // Initialize global bitmap pointers
    udf_read_space_bitmap(&mock_dev, 0, 0, sizeof(struct udf_space_bitmap) + 128);

    // Create FE at sector 1
    uint8_t fe_buf[UDF_SECTOR_SIZE];
    memset(fe_buf, 0, UDF_SECTOR_SIZE);
    struct udf_fe *fe_ptr = (struct udf_fe *)fe_buf;

    fe_ptr->tag.tag_id = UDF_TAG_FE;
    fe_ptr->icb_tag.file_type = 5; // Regular file
    fe_ptr->icb_tag.flags = UDF_ICB_FLAG_AD_SHORT;
    fe_ptr->info_length = 2048; // Initial size
    fe_ptr->alloc_desc_length = sizeof(struct udf_short_ad);
    
    struct udf_short_ad *ad = (struct udf_short_ad *)(fe_buf + sizeof(struct udf_fe));
    ad->length = 2048;
    ad->position = 2; // Sector 2

    memcpy(mock_disk + UDF_SECTOR_SIZE, fe_buf, UDF_SECTOR_SIZE);

    printf("Initial state: "); dump_ads(fe_ptr);

    // 1. Test Expansion (2048 -> 8192)
    printf("Testing Expansion: 2048 -> 8192\n");
    if (udf_truncate(&mock_dev, fe_ptr, 1, 8192) != 0) {
        printf("FAILED: udf_truncate expansion returned error\n");
        return 1;
    }

    struct udf_fe *disk_fe = (struct udf_fe *)(mock_disk + UDF_SECTOR_SIZE);
    printf("After expansion: "); dump_ads(disk_fe);
    printf("Bitmap: 0x%02x\n", bitmap_data[0]);

    if (disk_fe->info_length != 8192) {
        printf("FAILED: Expansion size mismatch: %ld\n", (long)disk_fe->info_length);
        return 1;
    }

    // 2. Test Shrinking (8192 -> 1024)
    printf("Testing Shrinking: 8192 -> 1024\n");
    if (udf_truncate(&mock_dev, fe_ptr, 1, 1024) != 0) {
        printf("FAILED: udf_truncate shrinking returned error\n");
        return 1;
    }

    printf("After shrinking: "); dump_ads(disk_fe);
    printf("Bitmap: 0x%02x\n", bitmap_data[0]);

    if (disk_fe->info_length != 1024) {
        printf("FAILED: Shrinking size mismatch: %ld\n", (long)disk_fe->info_length);
        return 1;
    }

    // Verify blocks were freed in bitmap
    // Sectors 3, 4, 5 should be free now.
    if (bitmap_data[0] & 0x38) { // 0x38 = 00111000 in binary (bits 3, 4, 5)
        printf("FAILED: Blocks not freed in bitmap: 0x%02x (expected flags 0x38 to be clear)\n", bitmap_data[0]);
        return 1;
    }

    printf("SUCCESS: host_test_udf_truncate_expand passed\n");
    return 0;
}
