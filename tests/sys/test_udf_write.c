/*
 * test_udf_write.c - Tests for UDF write-back functionality
 */

#include <drivers/console/console.h>
#include <string.h>
#include <fs/udf/udf.h>
#include <vfs/vfs.h>

/* External symbols from UDF driver */
extern struct udf_fs udf_ctx;
extern int udf_read_space_bitmap(fs_node_t *dev, uint32_t partition_start, uint32_t bitmap_loc, uint32_t bitmap_len);
extern uint32_t udf_alloc_block(void);
extern int udf_write_file(fs_node_t *dev, struct udf_fe *fe, uint32_t fe_block,
                   uint32_t offset, uint32_t size, const uint8_t *data);
extern int udf_truncate(fs_node_t *dev, struct udf_fe *fe, uint32_t fe_block, uint64_t new_size);

/* Mock Device */
static uint8_t mock_disk[UDF_SECTOR_SIZE * 16];
static uint32_t last_write_offset = 0;
static uint32_t last_write_size = 0;
static int write_called = 0;

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
    last_write_offset = (uint32_t)offset;
    last_write_size = size;
    write_called++;
    return size;
}

static fs_node_t mock_dev = {
    .read = mock_read,
    .write = mock_write,
};

static void test_udf_allocation_writeback(void) {
    kprintf("Running test_udf_allocation_writeback...\n");

    memset(mock_disk, 0, sizeof(mock_disk));
    write_called = 0;

    /* Setup Space Bitmap at sector 0 */
    struct udf_space_bitmap *sbm = (struct udf_space_bitmap *)mock_disk;
    sbm->tag.tag_id = UDF_TAG_SBD;
    sbm->tag.desc_version = 2;
    sbm->tag.tag_location = 0;
    sbm->num_bits = 64; /* 8 bytes */
    sbm->num_bytes = 8;

    /* Data starts at sizeof(struct udf_space_bitmap) */
    uint8_t *data = mock_disk + sizeof(struct udf_space_bitmap);

    /* Pre-allocate bit 0 to ensure we get block 1, distinguishing from error 0 */
    data[0] = 1;

    /* Calculate initial CRC/Checksum */
    sbm->tag.desc_crc_len = 8;
    sbm->tag.desc_crc = udf_crc(data, 8);
    sbm->tag.tag_checksum = udf_tag_checksum(&sbm->tag);

    /* Setup UDF context */
    udf_ctx.device = &mock_dev;

    /* Load Bitmap */
    /* partition_start=0, loc=0, len=sizeof(header)+8 */
    int res = udf_read_space_bitmap(&mock_dev, 0, 0, sizeof(struct udf_space_bitmap) + 8);
    if (res != 0) {
        kprintf("FAILED: udf_read_space_bitmap returned error\n");
        return;
    }

    /* Allocate a block */
    /* Should return block 1 (next free bit) */
    uint32_t block = udf_alloc_block();

    if (block != 1) {
        kprintf("FAILED: Expected block 1, got %d\n", block);
        return;
    }

    /* Verify write was called */
    if (write_called == 0) {
        kprintf("FAILED: No write occurred\n");
        return;
    }

    /* Verify bit 1 is set in mock_disk */
    /* data[0] should be 1 | 2 = 3 */
    if (data[0] != 3) {
        kprintf("FAILED: Bit 1 not set in disk. Byte is 0x%x\n", data[0]);
        return;
    }

    /* Verify CRC updated */
    uint16_t new_crc = udf_crc(data, 8);
    if (sbm->tag.desc_crc != new_crc) {
        kprintf("FAILED: CRC not updated correctly. Got 0x%x, expected 0x%x\n", sbm->tag.desc_crc, new_crc);
        return;
    }

    /* Verify Checksum updated */
    uint8_t new_sum = udf_tag_checksum(&sbm->tag);
    if (sbm->tag.tag_checksum != new_sum) {
        kprintf("FAILED: Tag checksum not valid. Got 0x%x, expected 0x%x\n", sbm->tag.tag_checksum, new_sum);
        return;
    }

    kprintf("test_udf_allocation_writeback: PASSED\n");
}

static void test_udf_large_file_write(void) {
    kprintf("Running test_udf_large_file_write...\n");

    memset(mock_disk, 0, sizeof(mock_disk));
    write_called = 0;

    /* Setup Space Bitmap at sector 0 */
    struct udf_space_bitmap *sbm = (struct udf_space_bitmap *)mock_disk;
    sbm->num_bits = 64; /* 8 bytes */
    sbm->num_bytes = 8;

    /* Mark sector 0 (SBM) and 1 (FE) as allocated */
    uint8_t *bitmap_data = mock_disk + sizeof(struct udf_space_bitmap);
    bitmap_data[0] = 0x03; // Bits 0 and 1 set

    /* Setup UDF context */
    udf_ctx.device = &mock_dev;
    udf_ctx.partition_start = 0;

    /* Load Bitmap */
    udf_read_space_bitmap(&mock_dev, 0, 0, sizeof(struct udf_space_bitmap) + 8);

    /* Create a File Entry at sector 1 */
    struct udf_fe fe;
    memset(&fe, 0, sizeof(struct udf_fe));
    fe.tag.tag_id = UDF_TAG_FE;
    fe.tag.tag_location = 1;
    fe.icb_tag.strategy_type = 4;
    fe.icb_tag.flags = UDF_ICB_FLAG_AD_INLINE; // Start with INLINE
    fe.info_length = 0;

    /* Write FE to disk sector 1 */
    memcpy(mock_disk + UDF_SECTOR_SIZE, &fe, sizeof(struct udf_fe));

    /* Prepare large data (4KB) */
    uint8_t large_data[4096];
    for (int i = 0; i < 4096; i++) large_data[i] = (uint8_t)(i % 256);

    /* Attempt to write 4KB to file */
    int res = udf_write_file(&mock_dev, &fe, 1, 0, 4096, large_data);

    if (res != 0) {
        kprintf("test_udf_large_file_write: FAILED (Expected success, got %d)\n", res);
        /* This is expected to fail currently, so we can consider it passed for reproduction if we want,
           but for the plan, we want to see it fail first. */
        return;
    }

    /* Verify FE updated to SHORT_AD */
    struct udf_fe *disk_fe = (struct udf_fe *)(mock_disk + UDF_SECTOR_SIZE);
    if ((disk_fe->icb_tag.flags & 0x7) != UDF_ICB_FLAG_AD_SHORT) {
        kprintf("test_udf_large_file_write: FAILED (FE flags not updated to SHORT_AD)\n");
        return;
    }

    /* Verify size */
    if (disk_fe->info_length != 4096) {
        kprintf("test_udf_large_file_write: FAILED (Size mismatch: %lld)\n", disk_fe->info_length);
        return;
    }

    /* Verify allocation descriptors */
    /* Expecting 2 blocks (4KB) allocated. They might be contiguous or not depending on allocator.
       Since mock disk is clean after sector 1, we expect sector 2 and 3. */

    /* Check data written to sectors 2 and 3 */
    if (memcmp(mock_disk + 2 * UDF_SECTOR_SIZE, large_data, 2048) != 0) {
        kprintf("test_udf_large_file_write: FAILED (Data mismatch at sector 2)\n");
        return;
    }
    if (memcmp(mock_disk + 3 * UDF_SECTOR_SIZE, large_data + 2048, 2048) != 0) {
        kprintf("test_udf_large_file_write: FAILED (Data mismatch at sector 3)\n");
        return;
    }

    kprintf("test_udf_large_file_write: PASSED\n");
}

static void test_udf_truncate_extent(void) {
    kprintf("Running test_udf_truncate_extent...\n");

    /* Reset mock disk */
    memset(mock_disk, 0, sizeof(mock_disk));
    write_called = 0;

    /* Setup Space Bitmap (Sector 0) similar to previous test */
    struct udf_space_bitmap *sbm = (struct udf_space_bitmap *)mock_disk;
    sbm->tag.tag_id = UDF_TAG_SBD;
    sbm->tag.desc_version = 2;
    sbm->tag.tag_location = 0;
    sbm->num_bits = 32; /* 4 bytes */
    sbm->num_bytes = 4;

    uint8_t *bitmap_data = mock_disk + sizeof(struct udf_space_bitmap);
    /* Mark block 2, 3 as allocated (where we put our file data) */
    /* Block 2 -> byte 0, bit 2. Block 3 -> byte 0, bit 3. */
    bitmap_data[0] |= (1 << 2) | (1 << 3);

    /* Calculate CRC */
    sbm->tag.desc_crc_len = 4;
    sbm->tag.desc_crc = udf_crc(bitmap_data, 4);
    sbm->tag.tag_checksum = udf_tag_checksum(&sbm->tag);

    /* Load bitmap */
    udf_ctx.device = &mock_dev;
    if (udf_read_space_bitmap(&mock_dev, 0, 0, sizeof(struct udf_space_bitmap) + 4) != 0) {
        kprintf("FAILED: Setup bitmap failed\n");
        return;
    }

    /* Setup File Entry at Sector 1 */
    uint32_t fe_sector = 1;
    struct udf_fe *fe = (struct udf_fe *)(mock_disk + fe_sector * UDF_SECTOR_SIZE);

    memset(fe, 0, sizeof(struct udf_fe));
    fe->tag.tag_id = UDF_TAG_FE;
    fe->tag.tag_location = fe_sector;
    fe->icb_tag.flags = UDF_ICB_FLAG_AD_SHORT; /* Extent-based */
    fe->info_length = 4096; /* 2 blocks */
    fe->alloc_desc_length = 2 * sizeof(struct udf_short_ad);

    /* Add Extents */
    /* Extents start after FE header + ext_attr_length (0) */
    struct udf_short_ad *ads = (struct udf_short_ad *)((uint8_t *)fe + sizeof(struct udf_fe));

    /* Extent 1: Block 2, Length 2048 */
    ads[0].length = 2048; /* Type 0: Allocated & Recorded */
    ads[0].position = 2;

    /* Extent 2: Block 3, Length 2048 */
    ads[1].length = 2048;
    ads[1].position = 3;

    /* Calculate FE checksum */
    fe->tag.tag_checksum = udf_tag_checksum(&fe->tag);

    /* Test 1: Truncate to 3000 bytes (partial block) */
    /* Should keep Block 2 fully, shrink Block 3 to 952 bytes */
    /* Both blocks should remain allocated */

    struct udf_fe fe_copy;
    memcpy(&fe_copy, fe, sizeof(struct udf_fe));

    int res = udf_truncate(&mock_dev, &fe_copy, fe_sector, 3000);

    if (res != 0) {
        kprintf("FAILED: udf_truncate returned %d (expected 0)\n", res);
        return;
    }

    if (fe_copy.info_length != 3000) {
        kprintf("FAILED: info_length = %llu (expected 3000)\n", fe_copy.info_length);
        return;
    }

    /* Verify disk update */
    struct udf_fe *disk_fe = (struct udf_fe *)(mock_disk + fe_sector * UDF_SECTOR_SIZE);
    struct udf_short_ad *disk_ads = (struct udf_short_ad *)((uint8_t *)disk_fe + sizeof(struct udf_fe));

    if (disk_fe->info_length != 3000) {
        kprintf("FAILED: Disk info_length = %llu\n", disk_fe->info_length);
        return;
    }

    if ((disk_ads[1].length & 0x3FFFFFFF) != 952) {
        kprintf("FAILED: AD[1] length = %u (expected 952)\n", disk_ads[1].length & 0x3FFFFFFF);
        return;
    }

    /* Verify Block 3 still allocated (bit 3 set) */
    if (!(bitmap_data[0] & (1 << 3))) {
         kprintf("FAILED: Block 3 was freed incorrectly\n");
         return;
    }

    /* Test 2: Truncate to 1024 bytes */
    /* Should keep Block 2 (partial), Remove Block 3 */
    /* Block 3 should be freed */

    res = udf_truncate(&mock_dev, &fe_copy, fe_sector, 1024);

    if (res != 0) {
        kprintf("FAILED: udf_truncate (2) returned %d\n", res);
        return;
    }

    if (fe_copy.info_length != 1024) {
        kprintf("FAILED: info_length (2) = %llu\n", fe_copy.info_length);
        return;
    }

    /* Verify disk update */
    /* AD[0] should be 1024, AD[1] should be gone */
    disk_ads = (struct udf_short_ad *)((uint8_t *)disk_fe + sizeof(struct udf_fe));

    if ((disk_ads[0].length & 0x3FFFFFFF) != 1024) {
        kprintf("FAILED: AD[0] length = %u (expected 1024)\n", disk_ads[0].length & 0x3FFFFFFF);
        return;
    }

    if (disk_fe->alloc_desc_length != sizeof(struct udf_short_ad)) {
        kprintf("FAILED: alloc_desc_length = %u (expected %u)\n",
                disk_fe->alloc_desc_length, sizeof(struct udf_short_ad));
        return;
    }

    /* Verify Block 3 Freed */
    /* Reload bitmap from disk (since udf_free_block writes it back) */
    /* udf_free_block updates the mock_disk directly */
    if (bitmap_data[0] & (1 << 3)) {
        kprintf("FAILED: Block 3 was NOT freed\n");
        return;
    }

    kprintf("test_udf_truncate_extent: PASSED\n");
}

void run_udf_write_tests(void) {
    test_udf_allocation_writeback();
    test_udf_large_file_write();
    test_udf_truncate_extent();
}
