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
static uint8_t mock_disk[UDF_SECTOR_SIZE * 16];

static uint32_t mock_read(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    if (offset + size > sizeof(mock_disk)) return 0;
    memcpy(buffer, mock_disk + offset, size);
    return size;
}

static uint32_t mock_write(fs_node_t *node, off_t offset, uint32_t size, const uint8_t *buffer) {
    (void)node;
    if (offset + size > sizeof(mock_disk)) return 0;
    memcpy(mock_disk + offset, buffer, size);
    return size;
}

static fs_node_t mock_dev = {
    .read = mock_read,
    .write = mock_write,
};

int main() {
    printf("Running host_test_udf_large_write...\n");

    // Setup (copy from test_udf_write.c)
    memset(mock_disk, 0, sizeof(mock_disk));

    // Setup Space Bitmap at sector 0
    struct udf_space_bitmap *sbm = (struct udf_space_bitmap *)mock_disk;
    sbm->tag.tag_id = UDF_TAG_SBD;
    sbm->num_bits = 64;
    sbm->num_bytes = 8;

    // Mark sector 0 and 1 as allocated
    uint8_t *bitmap_data = mock_disk + sizeof(struct udf_space_bitmap);
    bitmap_data[0] = 0x03;

    // Setup UDF context
    udf_ctx.device = &mock_dev;
    udf_ctx.partition_start = 0;

    // Initialize global bitmap pointers in udf_write.c
    udf_read_space_bitmap(&mock_dev, 0, 0, sizeof(struct udf_space_bitmap) + 8);

    // Create FE at sector 1
    struct udf_fe fe;
    memset(&fe, 0, sizeof(struct udf_fe));
    fe.tag.tag_id = UDF_TAG_FE;
    fe.icb_tag.flags = UDF_ICB_FLAG_AD_INLINE;

    memcpy(mock_disk + UDF_SECTOR_SIZE, &fe, sizeof(struct udf_fe));

    // Prepare large data
    uint8_t large_data[4096];
    for (int i = 0; i < 4096; i++) large_data[i] = (uint8_t)(i % 256);

    // Attempt write
    int res = udf_write_file(&mock_dev, &fe, 1, 0, 4096, large_data);

    if (res != 0) {
        printf("FAILED: udf_write_file returned %d\n", res);
        return 1; // Failure expected
    }

    // Verify FE updated to SHORT_AD
    struct udf_fe *disk_fe = (struct udf_fe *)(mock_disk + UDF_SECTOR_SIZE);
    if ((disk_fe->icb_tag.flags & 0x7) != UDF_ICB_FLAG_AD_SHORT) {
        printf("FAILED: FE flags not updated to SHORT_AD\n");
        return 1;
    }

    // Verify size
    if (disk_fe->info_length != 4096) {
        printf("FAILED: Size mismatch: %ld\n", (long)disk_fe->info_length);
        return 1;
    }

    // Verify data
    if (memcmp(mock_disk + 2 * UDF_SECTOR_SIZE, large_data, 2048) != 0) {
        printf("FAILED: Data mismatch at sector 2\n");
        return 1;
    }
    if (memcmp(mock_disk + 3 * UDF_SECTOR_SIZE, large_data + 2048, 2048) != 0) {
        printf("FAILED: Data mismatch at sector 3\n");
        return 1;
    }

    printf("SUCCESS: udf_write_file returned 0 and verified data\n");
    return 0;
}
