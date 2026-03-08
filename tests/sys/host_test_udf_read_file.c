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
    if (offset + size > mock_disk_sectors * UDF_SECTOR_SIZE) {
        if (offset >= mock_disk_sectors * UDF_SECTOR_SIZE) return 0;
        size = mock_disk_sectors * UDF_SECTOR_SIZE - offset;
    }
    memcpy(buffer, mock_disk + offset, size);
    return size;
}

void setup_mock_disk(uint32_t sectors) {
    mock_disk_sectors = sectors;
    mock_disk = calloc(sectors, UDF_SECTOR_SIZE);
}

void teardown_mock_disk(void) {
    free(mock_disk);
}

static void write_sector(uint32_t sector, void *data, uint32_t size) {
    assert(size <= UDF_SECTOR_SIZE);
    memcpy(mock_disk + (sector * UDF_SECTOR_SIZE), data, size);
}

// Tests
static void test_read_inline_file() {
    printf("Running test_read_inline_file...\n");

    struct udf_fs fs;
    // We need to allocate enough space for the FE *and* the inline data
    struct {
        struct udf_fe fe;
        uint8_t inline_data[1024];
    } mock_fe_data;
    struct udf_fe *fe = &mock_fe_data.fe;
    uint8_t buffer[1024];

    memset(&fs, 0, sizeof(fs));
    memset(&mock_fe_data, 0, sizeof(mock_fe_data));

    // Setup an inline file
    fe->icb_tag.flags = UDF_ICB_FLAG_AD_INLINE;
    fe->info_length = 13;
    fe->ext_attr_length = 0;

    // Create the allocation area and data
    uint8_t *alloc_area = ((uint8_t *)fe) + sizeof(struct udf_fe) + fe->ext_attr_length;
    const char *test_data = "Hello, World!";
    memcpy(alloc_area, test_data, fe->info_length);

    // Read full file
    memset(buffer, 0, sizeof(buffer));
    uint32_t read_bytes = udf_read_file(&fs, fe, 0, fe->info_length, buffer);
    assert(read_bytes == 13);
    assert(memcmp(buffer, test_data, 13) == 0);

    // Read with offset
    memset(buffer, 0, sizeof(buffer));
    read_bytes = udf_read_file(&fs, fe, 7, 6, buffer);
    assert(read_bytes == 6);
    assert(memcmp(buffer, "World!", 6) == 0);

    // Read past end
    memset(buffer, 0, sizeof(buffer));
    read_bytes = udf_read_file(&fs, fe, 0, 100, buffer);
    assert(read_bytes == 13);
    assert(memcmp(buffer, test_data, 13) == 0);

    // Read out of bounds
    read_bytes = udf_read_file(&fs, fe, 20, 10, buffer);
    assert(read_bytes == 0);

    printf("test_read_inline_file PASSED\n");
}

static void test_read_short_ad_file() {
    printf("Running test_read_short_ad_file...\n");
    setup_mock_disk(10);

    fs_node_t dev;
    dev.read = mock_read;

    struct udf_fs fs;
    memset(&fs, 0, sizeof(fs));
    fs.device = &dev;
    fs.partition_start = 0; // Absolute block

    // Define a struct large enough to hold the FE + alloc_area
    struct {
        struct udf_fe fe;
        struct udf_short_ad ads[4];
    } mock_fe_data;

    struct udf_fe *fe = &mock_fe_data.fe;

    memset(&mock_fe_data, 0, sizeof(mock_fe_data));
    fe->icb_tag.flags = UDF_ICB_FLAG_AD_SHORT;

    // We will have 4 extents:
    // 1. Extent 1: type 0 (recorded), length 1024, position 1
    // 2. Extent 2: type 1 (unallocated but recorded), length 512, position 2
    // 3. Extent 3: type 0 (recorded), length 2048, position 3
    // 4. Extent 4: type 3 (continuation), length 1024, position 4

    fe->info_length = 1024 + 512 + 2048 + 1024;
    fe->ext_attr_length = 0;
    fe->alloc_desc_length = 4 * sizeof(struct udf_short_ad);

    uint8_t *alloc_area = ((uint8_t *)fe) + sizeof(struct udf_fe) + fe->ext_attr_length;
    struct udf_short_ad *ads = (struct udf_short_ad *)alloc_area;

    ads[0].length = (0 << 30) | 1024;
    ads[0].position = 1;

    ads[1].length = (1 << 30) | 512;
    ads[1].position = 2;

    ads[2].length = (0 << 30) | 2048;
    ads[2].position = 3;

    ads[3].length = (3U << 30) | 1024;
    ads[3].position = 4;

    // Write some data to the mock disk
    uint8_t data1[1024];
    memset(data1, 'A', sizeof(data1));
    write_sector(1, data1, sizeof(data1));

    uint8_t data3[2048];
    memset(data3, 'B', sizeof(data3));
    write_sector(3, data3, sizeof(data3));

    uint8_t buffer[4096];

    // Read first extent partially
    memset(buffer, 0, sizeof(buffer));
    uint32_t read_bytes = udf_read_file(&fs, fe, 0, 500, buffer);
    assert(read_bytes == 500);
    for (int i = 0; i < 500; i++) assert(buffer[i] == 'A');

    // Read across extents (part of extent 1, all of extent 2, part of extent 3)
    memset(buffer, 0, sizeof(buffer));
    read_bytes = udf_read_file(&fs, fe, 1000, 24 + 512 + 10, buffer);
    assert(read_bytes == 24 + 512 + 10);
    for (int i = 0; i < 24; i++) assert(buffer[i] == 'A');
    for (int i = 24; i < 24 + 512; i++) assert(buffer[i] == 0); // Unrecorded extent reads as 0
    for (int i = 24 + 512; i < 24 + 512 + 10; i++) assert(buffer[i] == 'B');

    // Read full file
    memset(buffer, 0, sizeof(buffer));
    // It should stop at continuation!
    read_bytes = udf_read_file(&fs, fe, 0, 1024 + 512 + 2048 + 1024, buffer);
    assert(read_bytes == 1024 + 512 + 2048);
    for (int i = 0; i < 1024; i++) assert(buffer[i] == 'A');
    for (int i = 1024; i < 1024 + 512; i++) assert(buffer[i] == 0);
    for (int i = 1024 + 512; i < 1024 + 512 + 2048; i++) assert(buffer[i] == 'B');

    teardown_mock_disk();
    printf("test_read_short_ad_file PASSED\n");
}

static void test_read_short_ad_file_ext_read_limits() {
    printf("Running test_read_short_ad_file_ext_read_limits...\n");
    setup_mock_disk(10);

    fs_node_t dev;
    dev.read = mock_read;

    struct udf_fs fs;
    memset(&fs, 0, sizeof(fs));
    fs.device = &dev;
    fs.partition_start = 0; // Absolute block

    // Define a struct large enough to hold the FE + alloc_area
    struct {
        struct udf_fe fe;
        struct udf_short_ad ads[3];
    } mock_fe_data;

    struct udf_fe *fe = &mock_fe_data.fe;

    memset(&mock_fe_data, 0, sizeof(mock_fe_data));
    fe->icb_tag.flags = UDF_ICB_FLAG_AD_SHORT;

    // We will have 1 extent: type 0 (recorded), length 4096, position 1

    fe->info_length = 4096;
    fe->ext_attr_length = 0;
    fe->alloc_desc_length = 1 * sizeof(struct udf_short_ad);

    uint8_t *alloc_area = ((uint8_t *)fe) + sizeof(struct udf_fe) + fe->ext_attr_length;
    struct udf_short_ad *ads = (struct udf_short_ad *)alloc_area;

    ads[0].length = (0 << 30) | 4096;
    ads[0].position = 1;

    // Write some data to the mock disk
    uint8_t data1[2048];
    memset(data1, 'A', sizeof(data1));
    write_sector(1, data1, sizeof(data1));
    memset(data1, 'B', sizeof(data1));
    write_sector(2, data1, sizeof(data1));

    uint8_t buffer[4096];

    // Read spanning sector boundary inside one extent
    memset(buffer, 0, sizeof(buffer));
    uint32_t read_bytes = udf_read_file(&fs, fe, 2040, 16, buffer);
    assert(read_bytes == 16);
    for (int i = 0; i < 8; i++) assert(buffer[i] == 'A');
    for (int i = 8; i < 16; i++) assert(buffer[i] == 'B');

    teardown_mock_disk();
    printf("test_read_short_ad_file_ext_read_limits PASSED\n");
}

int main() {
    test_read_inline_file();
    test_read_short_ad_file();
    test_read_short_ad_file_ext_read_limits();
    printf("\nAll udf_read_file tests PASSED!\n");
    return 0;
}
