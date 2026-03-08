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

void test_udf_read_fe_success_fe() {
    printf("Running test_udf_read_fe_success_fe...\n");
    setup_mock_disk(100);

    fs_node_t dev;
    dev.read = mock_read;

    struct udf_fs fs;
    fs.device = &dev;
    fs.partition_start = 10;

    struct udf_long_ad icb;
    icb.block = 20;

    struct udf_fe fe_in, fe_out;
    memset(&fe_in, 0, sizeof(fe_in));
    fe_in.uid = 1000;
    fe_in.gid = 1000;

    write_descriptor(30, UDF_TAG_FE, &fe_in, sizeof(fe_in));

    int ret = udf_read_fe(&fs, &icb, &fe_out);
    assert(ret == 0);
    assert(fe_out.tag.tag_id == UDF_TAG_FE);
    assert(fe_out.uid == 1000);
    assert(fe_out.gid == 1000);

    teardown_mock_disk();
    printf("test_udf_read_fe_success_fe PASSED\n");
}

void test_udf_read_fe_success_efe() {
    printf("Running test_udf_read_fe_success_efe...\n");
    setup_mock_disk(100);

    fs_node_t dev;
    dev.read = mock_read;

    struct udf_fs fs;
    fs.device = &dev;
    fs.partition_start = 10;

    struct udf_long_ad icb;
    icb.block = 20;

    struct udf_efe efe_in;
    memset(&efe_in, 0, sizeof(efe_in));
    efe_in.uid = 2000;
    efe_in.gid = 2000;

    write_descriptor(30, UDF_TAG_EFE, &efe_in, sizeof(efe_in));

    struct udf_fe fe_out;
    int ret = udf_read_fe(&fs, &icb, &fe_out);
    assert(ret == 0);
    assert(fe_out.tag.tag_id == UDF_TAG_EFE);

    // Note that struct udf_fe and efe are compatible prefix
    struct udf_efe* efe_out = (struct udf_efe*)&fe_out;
    assert(efe_out->uid == 2000);
    assert(efe_out->gid == 2000);

    teardown_mock_disk();
    printf("test_udf_read_fe_success_efe PASSED\n");
}

void test_udf_read_fe_invalid_tag() {
    printf("Running test_udf_read_fe_invalid_tag...\n");
    setup_mock_disk(100);

    fs_node_t dev;
    dev.read = mock_read;

    struct udf_fs fs;
    fs.device = &dev;
    fs.partition_start = 10;

    struct udf_long_ad icb;
    icb.block = 20;

    struct udf_fe fe_in, fe_out;
    memset(&fe_in, 0, sizeof(fe_in));
    fe_in.uid = 1000;
    fe_in.gid = 1000;

    // Writing something else than FE or EFE
    write_descriptor(30, UDF_TAG_FID, &fe_in, sizeof(fe_in));

    int ret = udf_read_fe(&fs, &icb, &fe_out);
    assert(ret == -1);

    teardown_mock_disk();
    printf("test_udf_read_fe_invalid_tag PASSED\n");
}


void test_udf_read_fe_read_error() {
    printf("Running test_udf_read_fe_read_error...\n");
    setup_mock_disk(100);

    fs_node_t dev;
    dev.read = mock_read;

    struct udf_fs fs;
    fs.device = &dev;
    fs.partition_start = 10;

    struct udf_long_ad icb;
    icb.block = 1000; // Out of bounds

    struct udf_fe fe_out;

    int ret = udf_read_fe(&fs, &icb, &fe_out);
    assert(ret == -1);

    teardown_mock_disk();
    printf("test_udf_read_fe_read_error PASSED\n");
}

int main() {
    test_udf_read_fe_success_fe();
    test_udf_read_fe_success_efe();
    test_udf_read_fe_invalid_tag();
    test_udf_read_fe_read_error();

    printf("\nAll udf_read_fe tests PASSED!\n");
    return 0;
}
