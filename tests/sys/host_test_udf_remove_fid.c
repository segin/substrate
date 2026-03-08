#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

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
    // printf("%s", fmt);
}

void *kmalloc(size_t size) {
    return malloc(size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

// Mocks to allow compilation of udf_write.c
// Not actually used by udf_remove_fid
int _mock_udf_read_space_bitmap(fs_node_t *dev, uint32_t partition_start, uint32_t bitmap_loc, uint32_t bitmap_len) {
    (void)dev; (void)partition_start; (void)bitmap_loc; (void)bitmap_len;
    return 0;
}
uint32_t _mock_udf_alloc_block(void) { return 0; }
void _mock_udf_free_block(uint32_t block) { (void)block; }

#define HOST_TEST

// Include source (real file)
#include "../../sys/fs/udf/udf_write.c"

// Mocks for test
static uint8_t mock_disk[UDF_SECTOR_SIZE * 16];
static int mock_read_calls = 0;
static int mock_write_calls = 0;

static size_t mock_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    if (offset + size > sizeof(mock_disk)) return 0;
    memcpy(buffer, mock_disk + offset, size);
    mock_read_calls++;
    return size;
}

static size_t mock_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node;
    if (offset + size > sizeof(mock_disk)) return 0;
    memcpy(mock_disk + offset, buffer, size);
    mock_write_calls++;
    return size;
}

static fs_node_t mock_dev = {
    .read = mock_read,
    .write = mock_write
};

int main() {
    printf("Running host_test_udf_remove_fid...\n");

    // Setup
    memset(mock_disk, 0, sizeof(mock_disk));
    mock_read_calls = 0;
    mock_write_calls = 0;

    udf_ctx.device = &mock_dev;
    udf_ctx.partition_start = 0;

    uint32_t dir_block = 1;
    uint8_t *dir_data = mock_disk + dir_block * UDF_SECTOR_SIZE;
    uint32_t pos = 0;

    // FID 1: "testfile" (compression ID 8)
    struct udf_fid *fid1 = (struct udf_fid *)(dir_data + pos);
    fid1->tag.tag_id = UDF_TAG_FID;
    fid1->characteristics = 0;
    fid1->file_id_length = 9;
    fid1->impl_use_length = 0;
    uint32_t fid1_size = 38 + fid1->impl_use_length + fid1->file_id_length;
    fid1_size = (fid1_size + 3) & ~3;
    char *fname1 = (char *)fid1 + 38;
    fname1[0] = 8;
    memcpy(fname1 + 1, "testfile", 8);
    pos += fid1_size;

    // FID 2: "other" (compression ID 16, just to test else branch in name parsing)
    struct udf_fid *fid2 = (struct udf_fid *)(dir_data + pos);
    fid2->tag.tag_id = UDF_TAG_FID;
    fid2->characteristics = 0;
    fid2->file_id_length = 5;
    fid2->impl_use_length = 0;
    uint32_t fid2_size = 38 + fid2->impl_use_length + fid2->file_id_length;
    fid2_size = (fid2_size + 3) & ~3;
    char *fname2 = (char *)fid2 + 38;
    fname2[0] = 16;
    memcpy(fname2 + 1, "othe", 4);
    pos += fid2_size;

    // FID 3: "" (Empty file name, testing len=0 edge case)
    struct udf_fid *fid3 = (struct udf_fid *)(dir_data + pos);
    fid3->tag.tag_id = UDF_TAG_FID;
    fid3->characteristics = 0;
    fid3->file_id_length = 0;
    fid3->impl_use_length = 0;
    uint32_t fid3_size = 38 + fid3->impl_use_length + fid3->file_id_length;
    fid3_size = (fid3_size + 3) & ~3;
    pos += fid3_size;

    struct udf_fe dir_fe;
    memset(&dir_fe, 0, sizeof(dir_fe));
    dir_fe.info_length = pos;

    // Test removing existing file "testfile"
    int res = udf_remove_fid(&mock_dev, &dir_fe, dir_block, "testfile");
    if (res != 0) {
        printf("FAILED: udf_remove_fid returned %d, expected 0\n", res);
        return 1;
    }

    // Verify disk updated
    struct udf_fid *disk_fid1 = (struct udf_fid *)(mock_disk + dir_block * UDF_SECTOR_SIZE);
    if ((disk_fid1->characteristics & UDF_FID_DELETED) == 0) {
        printf("FAILED: testfile characteristics not updated: %02x\n", disk_fid1->characteristics);
        return 1;
    }

    // Test removing other file (branch coverage for ID != 8)
    res = udf_remove_fid(&mock_dev, &dir_fe, dir_block, "\x10othe");
    if (res != 0) {
        printf("FAILED: udf_remove_fid returned %d, expected 0\n", res);
        return 1;
    }

    // Verify disk updated for other file
    struct udf_fid *disk_fid2 = (struct udf_fid *)(mock_disk + dir_block * UDF_SECTOR_SIZE + fid1_size);
    if ((disk_fid2->characteristics & UDF_FID_DELETED) == 0) {
        printf("FAILED: other file characteristics not updated: %02x\n", disk_fid2->characteristics);
        return 1;
    }

    // Test removing empty file name
    res = udf_remove_fid(&mock_dev, &dir_fe, dir_block, "");
    if (res != 0) {
        printf("FAILED: udf_remove_fid returned %d, expected 0\n", res);
        return 1;
    }

    // Verify disk updated for empty file name
    struct udf_fid *disk_fid3 = (struct udf_fid *)(mock_disk + dir_block * UDF_SECTOR_SIZE + fid1_size + fid2_size);
    if ((disk_fid3->characteristics & UDF_FID_DELETED) == 0) {
        printf("FAILED: empty file characteristics not updated: %02x\n", disk_fid3->characteristics);
        return 1;
    }

    // Test removing non-existent file
    res = udf_remove_fid(&mock_dev, &dir_fe, dir_block, "nonexistent");
    if (res != -1) {
        printf("FAILED: udf_remove_fid returned %d for nonexistent file, expected -1\n", res);
        return 1;
    }

    printf("SUCCESS: udf_remove_fid tested successfully\n");
    return 0;
}
