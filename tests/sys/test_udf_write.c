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

/* Mock Device */
static uint8_t mock_disk[UDF_SECTOR_SIZE * 4];
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

void run_udf_write_tests(void) {
    test_udf_allocation_writeback();
}
