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
void test_fsd_success() {
    printf("Running test_fsd_success...\n");
    setup_mock_disk(100);

    fs_node_t dev;
    dev.read = mock_read;

    struct udf_fs fs;
    memset(&fs, 0, sizeof(fs));
    fs.partition_start = 10;
    fs.sector_size = UDF_SECTOR_SIZE;

    struct udf_lvd lvd;
    memset(&lvd, 0, sizeof(lvd));
    lvd.fsd_location.block = 5;

    struct udf_fsd fsd_in, fsd_out;
    memset(&fsd_in, 0, sizeof(fsd_in));

    // Some arbitrary data to verify output
    fsd_in.fileset_number = 12345;

    // Sector = partition_start (10) + fsd_location.block (5) = 15
    write_descriptor(15, UDF_TAG_FSD, &fsd_in, sizeof(fsd_in));

    int ret = udf_read_fsd(&dev, &fs, &lvd, &fsd_out);
    assert(ret == 0);
    assert(fsd_out.tag.tag_id == UDF_TAG_FSD);
    assert(fsd_out.fileset_number == 12345);

    teardown_mock_disk();
    printf("test_fsd_success PASSED\n");
}

void test_fsd_read_fail() {
    printf("Running test_fsd_read_fail...\n");
    setup_mock_disk(10); // Very small disk

    fs_node_t dev;
    dev.read = mock_read;

    struct udf_fs fs;
    memset(&fs, 0, sizeof(fs));
    fs.partition_start = 10;
    fs.sector_size = UDF_SECTOR_SIZE;

    struct udf_lvd lvd;
    memset(&lvd, 0, sizeof(lvd));
    lvd.fsd_location.block = 5;

    struct udf_fsd fsd_out;

    // Sector = 15, but disk only has 10 sectors. This should fail mock_read.
    int ret = udf_read_fsd(&dev, &fs, &lvd, &fsd_out);
    assert(ret == -1);

    teardown_mock_disk();
    printf("test_fsd_read_fail PASSED\n");
}

void test_fsd_invalid_tag() {
    printf("Running test_fsd_invalid_tag...\n");
    setup_mock_disk(100);

    fs_node_t dev;
    dev.read = mock_read;

    struct udf_fs fs;
    memset(&fs, 0, sizeof(fs));
    fs.partition_start = 10;
    fs.sector_size = UDF_SECTOR_SIZE;

    struct udf_lvd lvd;
    memset(&lvd, 0, sizeof(lvd));
    lvd.fsd_location.block = 5;

    struct udf_fsd fsd_in, fsd_out;
    memset(&fsd_in, 0, sizeof(fsd_in));

    // Writing a PD instead of an FSD tag
    write_descriptor(15, UDF_TAG_PARTITION_D, &fsd_in, sizeof(fsd_in));

    int ret = udf_read_fsd(&dev, &fs, &lvd, &fsd_out);
    assert(ret == -1);

    teardown_mock_disk();
    printf("test_fsd_invalid_tag PASSED\n");
}

int main() {
    test_fsd_success();
    test_fsd_read_fail();
    test_fsd_invalid_tag();

    printf("\nAll udf_read_fsd tests PASSED!\n");
    return 0;
}
