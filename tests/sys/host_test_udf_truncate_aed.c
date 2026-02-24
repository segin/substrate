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
// 8MB disk to hold enough data for AEDs
#define MOCK_DISK_SIZE (UDF_SECTOR_SIZE * 4096)
static uint8_t mock_disk[MOCK_DISK_SIZE];

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

static void dump_fe_stats(struct udf_fe *fe) {
    uint32_t num_ads = fe->alloc_desc_length / sizeof(struct udf_short_ad);
    printf("FE Stats: size=%llu, ADs=%u, flags=0x%x\n",
           (unsigned long long)fe->info_length, num_ads, fe->icb_tag.flags & 0x7);
}

int main() {
    printf("Running host_test_udf_truncate_aed...\n");

    memset(mock_disk, 0, sizeof(mock_disk));

    // Setup Space Bitmap at sector 0
    // 4096 sectors total.
    struct udf_space_bitmap *sbm = (struct udf_space_bitmap *)mock_disk;
    sbm->tag.tag_id = UDF_TAG_SBD;
    sbm->num_bits = 4096;
    sbm->num_bytes = 512;

    // Mark sector 0 (SBM), 1 (FE) as allocated
    uint8_t *bitmap_data = mock_disk + sizeof(struct udf_space_bitmap);
    bitmap_data[0] |= 0x03; // Bits 0 and 1 set

    // Setup UDF context
    udf_ctx.device = &mock_dev;
    udf_ctx.partition_start = 0;

    // Manually mark every ODD sector starting from 3 as allocated to force fragmentation
    for (int i = 3; i < 4096; i += 2) {
        int byte = i / 8;
        int bit = i % 8;
        bitmap_data[byte] |= (1 << bit);
    }

    // Initialize global bitmap pointers
    udf_read_space_bitmap(&mock_dev, 0, 0, sizeof(struct udf_space_bitmap) + 256);

    // Create FE at sector 1
    uint8_t fe_buf[UDF_SECTOR_SIZE];
    memset(fe_buf, 0, UDF_SECTOR_SIZE);
    struct udf_fe *fe = (struct udf_fe *)fe_buf;
    fe->tag.tag_id = UDF_TAG_FE;
    fe->icb_tag.file_type = 5;
    fe->icb_tag.flags = UDF_ICB_FLAG_AD_SHORT;
    fe->info_length = 0;
    fe->alloc_desc_length = 0;

    // Determine how many extents fit in FE
    // FE overhead ~176 bytes + Extended Attributes length (0).
    // Max ADs ~ (2048 - 176) / 8 = 234.
    // We want to force at least one AED. So let's write enough data for 300 extents.
    // 300 extents * 2048 bytes = 600KB.
    // Let's write 2MB to ensure overflow.
    uint32_t write_size = 2 * 1024 * 1024;
    uint8_t *data = malloc(write_size);
    memset(data, 0xAA, write_size); // Fill with pattern

    printf("Writing 2MB data to force AED creation...\n");
    if (udf_write_file(&mock_dev, fe, 1, 0, write_size, data) != 0) {
        printf("FAILED: udf_write_file returned error\n");
        free(data);
        return 1;
    }

    // Read FE from disk to get ADs (udf_write_file only copies the header back)
    mock_dev.read(&mock_dev, UDF_SECTOR_SIZE, UDF_SECTOR_SIZE, fe_buf);

    dump_fe_stats(fe);

    // Check if we actually have AEDs
    // Loop through FE ADs to find type 3
    int has_aed = 0;
    uint32_t num_ads = fe->alloc_desc_length / sizeof(struct udf_short_ad);
    struct udf_short_ad *ads = (struct udf_short_ad *)((uint8_t *)fe + sizeof(struct udf_fe));
    for (uint32_t i = 0; i < num_ads; i++) {
        uint32_t type = (ads[i].length >> 30) & 0x3;
        if (type == 3) {
            has_aed = 1;
            printf("Found AED link at index %u, pointing to block %u\n", i, ads[i].position);
            break;
        }
    }

    if (!has_aed) {
        printf("FAILED: No AED created. Test invalid. Fragmentation failed?\n");
        printf("Last few ADs:\n");
        for (uint32_t i = num_ads - 5; i < num_ads; i++) {
             uint32_t len = ads[i].length & 0x3FFFFFFF;
             uint32_t type = (ads[i].length >> 30) & 0x3;
             uint32_t pos = ads[i].position;
             printf("[%u] type=%u len=%u pos=%u\n", i, type, len, pos);
        }
        free(data);
        return 1;
    }

    // Now truncate to 2048 bytes (1 block).
    // This should free all AEDs and most data blocks.
    printf("Truncating to 2048 bytes...\n");
    if (udf_truncate(&mock_dev, fe, 1, 2048) != 0) {
        printf("FAILED: udf_truncate returned error\n");
        free(data);
        return 1;
    }

    dump_fe_stats(fe);

    // Verify FE has no AED links now
    num_ads = fe->alloc_desc_length / sizeof(struct udf_short_ad);
    ads = (struct udf_short_ad *)((uint8_t *)fe + sizeof(struct udf_fe));
    for (uint32_t i = 0; i < num_ads; i++) {
        uint32_t type = (ads[i].length >> 30) & 0x3;
        if (type == 3) {
            printf("FAILED: AED link still present in FE after truncation!\n");
            // This is expected failure for the bug
        }
    }

    // Count free blocks in bitmap.
    // We marked 1024 blocks as "used" (odd ones) + SBM + FE = 1026 blocks.
    // We allocated ~512 even blocks for data.
    // Then we created some AED blocks (also even).
    // After truncation, we should have only 1 data block (sector 2) used.
    // All other even blocks should be free.

    // Check sector 2 (should be used)
    if (!(bitmap_data[0] & 0x04)) {
        printf("FAILED: Sector 2 should be used (first data block)\n");
        free(data);
        return 1;
    }

    // Check random even sectors that were used (e.g. 4, 6, 100...)
    // They should be free now.
    int unfree_count = 0;
    for (int i = 4; i < 4096; i += 2) {
        int byte = i / 8;
        int bit = i % 8;
        if (bitmap_data[byte] & (1 << bit)) {
            unfree_count++;
        }
    }

    if (unfree_count > 0) {
        printf("FAILED: %d even blocks are still marked as allocated! Leak detected.\n", unfree_count);
        free(data);
        return 1;
    }

    printf("SUCCESS: host_test_udf_truncate_aed passed\n");
    free(data);
    return 0;
}
