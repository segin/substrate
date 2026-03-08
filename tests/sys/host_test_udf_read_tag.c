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
void test_read_tag_success() {
    printf("Running test_read_tag_success...\n");
    setup_mock_disk(10);

    fs_node_t dev;
    dev.read = mock_read;

    struct udf_pvd pvd_in;
    memset(&pvd_in, 0, sizeof(pvd_in));

    write_descriptor(5, UDF_TAG_PRIMARY_VD, &pvd_in, sizeof(pvd_in));

    struct udf_tag tag_out;
    uint8_t buffer[UDF_SECTOR_SIZE];

    int ret = udf_read_tag(&dev, 5, &tag_out, buffer, UDF_SECTOR_SIZE);

    assert(ret == 0);
    assert(tag_out.tag_id == UDF_TAG_PRIMARY_VD);
    assert(tag_out.tag_location == 5);

    teardown_mock_disk();
    printf("test_read_tag_success PASSED\n");
}

void test_read_tag_null_dev() {
    printf("Running test_read_tag_null_dev...\n");

    struct udf_tag tag_out;
    uint8_t buffer[UDF_SECTOR_SIZE];

    int ret = udf_read_tag(NULL, 5, &tag_out, buffer, UDF_SECTOR_SIZE);
    assert(ret == -1);

    fs_node_t dev_no_read;
    memset(&dev_no_read, 0, sizeof(dev_no_read));

    ret = udf_read_tag(&dev_no_read, 5, &tag_out, buffer, UDF_SECTOR_SIZE);
    assert(ret == -1);

    printf("test_read_tag_null_dev PASSED\n");
}

void test_read_tag_read_fail() {
    printf("Running test_read_tag_read_fail...\n");
    setup_mock_disk(10);

    fs_node_t dev;
    dev.read = mock_read;

    struct udf_tag tag_out;
    uint8_t buffer[UDF_SECTOR_SIZE];

    int ret = udf_read_tag(&dev, 15, &tag_out, buffer, UDF_SECTOR_SIZE);
    assert(ret == -1);

    teardown_mock_disk();
    printf("test_read_tag_read_fail PASSED\n");
}

void test_read_tag_checksum_mismatch() {
    printf("Running test_read_tag_checksum_mismatch...\n");
    setup_mock_disk(10);

    fs_node_t dev;
    dev.read = mock_read;

    struct udf_pvd pvd_in;
    memset(&pvd_in, 0, sizeof(pvd_in));

    write_descriptor(5, UDF_TAG_PRIMARY_VD, &pvd_in, sizeof(pvd_in));

    // Corrupt the checksum
    struct udf_tag *tag = (struct udf_tag *)(mock_disk + (5 * UDF_SECTOR_SIZE));
    tag->tag_checksum++;

    struct udf_tag tag_out;
    uint8_t buffer[UDF_SECTOR_SIZE];

    int ret = udf_read_tag(&dev, 5, &tag_out, buffer, UDF_SECTOR_SIZE);
    assert(ret == -1);

    teardown_mock_disk();
    printf("test_read_tag_checksum_mismatch PASSED\n");
}

void test_read_tag_location_mismatch() {
    printf("Running test_read_tag_location_mismatch...\n");
    setup_mock_disk(10);

    fs_node_t dev;
    dev.read = mock_read;

    struct udf_pvd pvd_in;
    memset(&pvd_in, 0, sizeof(pvd_in));

    // Write a tag with location 6 at sector 5
    write_descriptor(6, UDF_TAG_PRIMARY_VD, &pvd_in, sizeof(pvd_in));
    // Move it manually to sector 5
    memcpy(mock_disk + (5 * UDF_SECTOR_SIZE), mock_disk + (6 * UDF_SECTOR_SIZE), UDF_SECTOR_SIZE);

    struct udf_tag tag_out;
    uint8_t buffer[UDF_SECTOR_SIZE];

    int ret = udf_read_tag(&dev, 5, &tag_out, buffer, UDF_SECTOR_SIZE);
    assert(ret == -1);

    teardown_mock_disk();
    printf("test_read_tag_location_mismatch PASSED\n");
}

void test_read_tag_crc_mismatch() {
    printf("Running test_read_tag_crc_mismatch...\n");
    setup_mock_disk(10);

    fs_node_t dev;
    dev.read = mock_read;

    struct udf_pvd pvd_in;
    memset(&pvd_in, 0, sizeof(pvd_in));

    write_descriptor(5, UDF_TAG_PRIMARY_VD, &pvd_in, sizeof(pvd_in));

    // Corrupt the CRC
    struct udf_tag *tag = (struct udf_tag *)(mock_disk + (5 * UDF_SECTOR_SIZE));
    tag->desc_crc++;
    tag->tag_checksum = udf_tag_checksum(tag); // Fix checksum

    struct udf_tag tag_out;
    uint8_t buffer[UDF_SECTOR_SIZE];

    int ret = udf_read_tag(&dev, 5, &tag_out, buffer, UDF_SECTOR_SIZE);
    assert(ret == -1);

    teardown_mock_disk();
    printf("test_read_tag_crc_mismatch PASSED\n");
}

int main() {
    test_read_tag_success();
    test_read_tag_null_dev();
    test_read_tag_read_fail();
    test_read_tag_checksum_mismatch();
    test_read_tag_location_mismatch();
    test_read_tag_crc_mismatch();

    printf("\nAll udf_read_tag tests PASSED!\n");
    return 0;
}
