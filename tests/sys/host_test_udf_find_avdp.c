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

void test_avdp_at_256() {
    printf("Running test_avdp_at_256...\n");
    setup_mock_disk(1000);

    fs_node_t dev;
    dev.read = mock_read;
    dev.length = 1000 * UDF_SECTOR_SIZE;

    struct udf_avdp avdp_in, avdp_out;
    memset(&avdp_in, 0, sizeof(avdp_in));

    // Valid AVDP at 256
    write_descriptor(256, UDF_TAG_ANCHOR_VDP, &avdp_in, sizeof(avdp_in));

    int ret = udf_find_avdp(&dev, &avdp_out);
    assert(ret == 0);
    assert(avdp_out.tag.tag_id == UDF_TAG_ANCHOR_VDP);
    assert(avdp_out.tag.tag_location == 256);

    teardown_mock_disk();
    printf("test_avdp_at_256 PASSED\n");
}

void test_avdp_at_last() {
    printf("Running test_avdp_at_last...\n");
    setup_mock_disk(1000);

    fs_node_t dev;
    dev.read = mock_read;
    dev.length = 1000 * UDF_SECTOR_SIZE;

    struct udf_avdp avdp_in, avdp_out;
    memset(&avdp_in, 0, sizeof(avdp_in));

    // No AVDP at 256. Valid AVDP at last sector (999)
    write_descriptor(999, UDF_TAG_ANCHOR_VDP, &avdp_in, sizeof(avdp_in));

    int ret = udf_find_avdp(&dev, &avdp_out);
    assert(ret == 0);
    assert(avdp_out.tag.tag_id == UDF_TAG_ANCHOR_VDP);
    assert(avdp_out.tag.tag_location == 999);

    teardown_mock_disk();
    printf("test_avdp_at_last PASSED\n");
}

void test_avdp_at_last_256() {
    printf("Running test_avdp_at_last_256...\n");
    setup_mock_disk(1000);

    fs_node_t dev;
    dev.read = mock_read;
    dev.length = 1000 * UDF_SECTOR_SIZE;

    struct udf_avdp avdp_in, avdp_out;
    memset(&avdp_in, 0, sizeof(avdp_in));

    // No AVDP at 256 or 999. Valid AVDP at last - 256 (743)
    write_descriptor(999 - 256, UDF_TAG_ANCHOR_VDP, &avdp_in, sizeof(avdp_in));

    int ret = udf_find_avdp(&dev, &avdp_out);
    assert(ret == 0);
    assert(avdp_out.tag.tag_id == UDF_TAG_ANCHOR_VDP);
    assert(avdp_out.tag.tag_location == 743);

    teardown_mock_disk();
    printf("test_avdp_at_last_256 PASSED\n");
}

void test_avdp_not_found() {
    printf("Running test_avdp_not_found...\n");
    setup_mock_disk(1000);

    fs_node_t dev;
    dev.read = mock_read;
    dev.length = 1000 * UDF_SECTOR_SIZE;

    struct udf_avdp avdp_out;

    // Empty disk, no AVDP

    int ret = udf_find_avdp(&dev, &avdp_out);
    assert(ret == -1);

    teardown_mock_disk();
    printf("test_avdp_not_found PASSED\n");
}

void test_avdp_short_disk() {
    printf("Running test_avdp_short_disk...\n");
    setup_mock_disk(100);

    fs_node_t dev;
    dev.read = mock_read;
    dev.length = 100 * UDF_SECTOR_SIZE;

    struct udf_avdp avdp_out;

    // Disk is too short, should try 256 and fail, but skip last/last-256

    int ret = udf_find_avdp(&dev, &avdp_out);
    assert(ret == -1);

    teardown_mock_disk();
    printf("test_avdp_short_disk PASSED\n");
}

int main() {
    test_avdp_at_256();
    test_avdp_at_last();
    test_avdp_at_last_256();
    test_avdp_not_found();
    test_avdp_short_disk();

    printf("\nAll udf_find_avdp tests PASSED!\n");
    return 0;
}
