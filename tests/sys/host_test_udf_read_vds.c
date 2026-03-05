#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>

// Mock kprint
void kprint(const char *str) {
    // printf("kprint: %s", str);
}

// Mock kmalloc/free
void *kmalloc(size_t size) { return calloc(1, size); }
void *kzalloc(size_t size) { return calloc(1, size); }
void kfree(void *ptr, size_t size) { (void)size; free(ptr); }

#define HOST_TEST
#include <fs/udf/udf.h>
#include <vfs/vfs.h>
#include <vfs/vnode.h>

void vfs_register_filesystem(filesystem_t *fs) { (void)fs; }
int getnewvnode(const char *tag, struct mount *mp, struct vnodeops *vops, struct vnode **vpp) { return 0; }
struct process *current_process = NULL;

// Stub out externals
int udf_read_space_bitmap(fs_node_t *dev, uint32_t partition_start, uint32_t bitmap_loc, uint32_t bitmap_len) { return 0; }
uint32_t udf_alloc_block(void) { return 0; }
void udf_free_block(uint32_t block) { }
int udf_create_fe(fs_node_t *dev, uint32_t block, uint8_t file_type, uint32_t uid, uint32_t gid, uint32_t permissions) { return 0; }
int udf_add_fid(fs_node_t *dev, struct udf_fe *dir_fe, uint32_t dir_block, const char *name, struct udf_long_ad *icb, uint8_t characteristics) { return 0; }

// Include the source
#include "../../sys/fs/udf/udf.c"

// Test infrastructure
static uint8_t *mock_disk;
static uint32_t mock_disk_sectors;

static size_t mock_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    if (offset + size > mock_disk_sectors * UDF_SECTOR_SIZE) return 0;
    memcpy(buffer, mock_disk + offset, size);
    return size;
}

void setup_mock_disk(uint32_t sectors) {
    mock_disk_sectors = sectors;
    mock_disk = calloc(sectors, UDF_SECTOR_SIZE);
}

void teardown_mock_disk() {
    free(mock_disk);
    mock_disk = NULL;
}

void write_sector(uint32_t sector, void *data, uint32_t size) {
    assert(size <= UDF_SECTOR_SIZE);
    memcpy(mock_disk + (sector * UDF_SECTOR_SIZE), data, size);
}

void write_descriptor(uint32_t sector, uint16_t tag_id, void *desc, uint32_t desc_size) {
    struct udf_tag *tag = (struct udf_tag *)desc;
    tag->tag_id = tag_id;
    tag->tag_location = sector;
    tag->desc_version = 3;
    tag->desc_crc_len = desc_size - sizeof(struct udf_tag);
    tag->desc_crc = udf_crc((uint8_t *)desc + sizeof(struct udf_tag), tag->desc_crc_len);
    tag->tag_checksum = udf_tag_checksum(tag);

    write_sector(sector, desc, desc_size);
}

// Test cases
void test_vds_success() {
    printf("Running test_vds_success...\n");
    setup_mock_disk(100);

    fs_node_t dev;
    dev.read = mock_read;

    struct udf_extent_ad vds_extent = { .location = 50, .length = 10 * UDF_SECTOR_SIZE };
    struct udf_pvd pvd_in, pvd_out;
    struct udf_pd pd_in, pd_out;
    struct udf_lvd lvd_in, lvd_out;

    memset(&pvd_in, 0, sizeof(pvd_in));
    memset(&pd_in, 0, sizeof(pd_in));
    memset(&lvd_in, 0, sizeof(lvd_in));

    write_descriptor(50, UDF_TAG_PRIMARY_VD, &pvd_in, sizeof(pvd_in));
    write_descriptor(51, UDF_TAG_PARTITION_D, &pd_in, sizeof(pd_in));
    write_descriptor(52, UDF_TAG_LOGICAL_VD, &lvd_in, sizeof(lvd_in));

    int ret = udf_read_vds(&dev, &vds_extent, &pvd_out, &pd_out, &lvd_out);
    assert(ret == 0);
    assert(pvd_out.tag.tag_id == UDF_TAG_PRIMARY_VD);
    assert(pd_out.tag.tag_id == UDF_TAG_PARTITION_D);
    assert(lvd_out.tag.tag_id == UDF_TAG_LOGICAL_VD);

    teardown_mock_disk();
    printf("test_vds_success PASSED\n");
}

void test_vds_incomplete() {
    printf("Running test_vds_incomplete...\n");
    setup_mock_disk(100);

    fs_node_t dev;
    dev.read = mock_read;

    struct udf_extent_ad vds_extent = { .location = 50, .length = 10 * UDF_SECTOR_SIZE };
    struct udf_pvd pvd_in, pvd_out;
    struct udf_pd pd_in, pd_out;
    struct udf_lvd lvd_out;

    memset(&pvd_in, 0, sizeof(pvd_in));
    memset(&pd_in, 0, sizeof(pd_in));

    write_descriptor(50, UDF_TAG_PRIMARY_VD, &pvd_in, sizeof(pvd_in));
    write_descriptor(51, UDF_TAG_PARTITION_D, &pd_in, sizeof(pd_in));
    // Logical VD missing

    int ret = udf_read_vds(&dev, &vds_extent, &pvd_out, &pd_out, &lvd_out);
    assert(ret == -1);

    teardown_mock_disk();
    printf("test_vds_incomplete PASSED\n");
}

void test_vds_terminating() {
    printf("Running test_vds_terminating...\n");
    setup_mock_disk(100);

    fs_node_t dev;
    dev.read = mock_read;

    struct udf_extent_ad vds_extent = { .location = 50, .length = 10 * UDF_SECTOR_SIZE };
    struct udf_pvd pvd_in, pvd_out;
    struct udf_pd pd_in, pd_out;
    struct udf_lvd lvd_in, lvd_out;
    struct udf_tag term_tag;

    memset(&pvd_in, 0, sizeof(pvd_in));
    memset(&pd_in, 0, sizeof(pd_in));
    memset(&lvd_in, 0, sizeof(lvd_in));
    memset(&term_tag, 0, sizeof(term_tag));

    write_descriptor(50, UDF_TAG_PRIMARY_VD, &pvd_in, sizeof(pvd_in));
    write_descriptor(51, UDF_TAG_TERMINATING, &term_tag, sizeof(term_tag));
    write_descriptor(52, UDF_TAG_PARTITION_D, &pd_in, sizeof(pd_in));
    write_descriptor(53, UDF_TAG_LOGICAL_VD, &lvd_in, sizeof(lvd_in));

    // Should stop at sector 51 and fail because PD and LVD not found yet
    int ret = udf_read_vds(&dev, &vds_extent, &pvd_out, &pd_out, &lvd_out);
    assert(ret == -1);

    teardown_mock_disk();
    printf("test_vds_terminating PASSED\n");
}

void test_vds_invalid_tag() {
    printf("Running test_vds_invalid_tag...\n");
    setup_mock_disk(100);

    fs_node_t dev;
    dev.read = mock_read;

    struct udf_extent_ad vds_extent = { .location = 50, .length = 10 * UDF_SECTOR_SIZE };
    struct udf_pvd pvd_in, pvd_out;
    struct udf_pd pd_in, pd_out;
    struct udf_lvd lvd_in, lvd_out;

    memset(&pvd_in, 0, sizeof(pvd_in));
    memset(&pd_in, 0, sizeof(pd_in));
    memset(&lvd_in, 0, sizeof(lvd_in));

    write_descriptor(50, UDF_TAG_PRIMARY_VD, &pvd_in, sizeof(pvd_in));

    // Invalid tag at 51 (bad checksum)
    write_descriptor(51, UDF_TAG_PARTITION_D, &pd_in, sizeof(pd_in));
    struct udf_tag *tag = (struct udf_tag *)(mock_disk + (51 * UDF_SECTOR_SIZE));
    tag->tag_checksum++;

    write_descriptor(52, UDF_TAG_PARTITION_D, &pd_in, sizeof(pd_in));
    write_descriptor(53, UDF_TAG_LOGICAL_VD, &lvd_in, sizeof(lvd_in));

    int ret = udf_read_vds(&dev, &vds_extent, &pvd_out, &pd_out, &lvd_out);
    assert(ret == 0); // Should skip 51 and find PD at 52
    assert(pvd_out.tag.tag_id == UDF_TAG_PRIMARY_VD);
    assert(pd_out.tag.tag_id == UDF_TAG_PARTITION_D);
    assert(pd_out.tag.tag_location == 52);
    assert(lvd_out.tag.tag_id == UDF_TAG_LOGICAL_VD);

    teardown_mock_disk();
    printf("test_vds_invalid_tag PASSED\n");
}

void test_vds_crc_mismatch() {
    printf("Running test_vds_crc_mismatch...\n");
    setup_mock_disk(100);

    fs_node_t dev;
    dev.read = mock_read;

    struct udf_extent_ad vds_extent = { .location = 50, .length = 10 * UDF_SECTOR_SIZE };
    struct udf_pvd pvd_in, pvd_out;
    struct udf_pd pd_in, pd_out;
    struct udf_lvd lvd_in, lvd_out;

    memset(&pvd_in, 0, sizeof(pvd_in));
    memset(&pd_in, 0, sizeof(pd_in));
    memset(&lvd_in, 0, sizeof(lvd_in));

    write_descriptor(50, UDF_TAG_PRIMARY_VD, &pvd_in, sizeof(pvd_in));

    // CRC mismatch at 51
    write_descriptor(51, UDF_TAG_PARTITION_D, &pd_in, sizeof(pd_in));
    struct udf_tag *tag = (struct udf_tag *)(mock_disk + (51 * UDF_SECTOR_SIZE));
    tag->desc_crc++;
    tag->tag_checksum = udf_tag_checksum(tag); // Fix checksum so it only fails CRC

    write_descriptor(52, UDF_TAG_PARTITION_D, &pd_in, sizeof(pd_in));
    write_descriptor(53, UDF_TAG_LOGICAL_VD, &lvd_in, sizeof(lvd_in));

    int ret = udf_read_vds(&dev, &vds_extent, &pvd_out, &pd_out, &lvd_out);
    assert(ret == 0); // Should skip 51 and find PD at 52
    assert(pd_out.tag.tag_location == 52);

    teardown_mock_disk();
    printf("test_vds_crc_mismatch PASSED\n");
}

int main() {
    test_vds_success();
    test_vds_incomplete();
    test_vds_terminating();
    test_vds_invalid_tag();
    test_vds_crc_mismatch();

    printf("\nAll udf_read_vds tests PASSED!\n");
    return 0;
}
