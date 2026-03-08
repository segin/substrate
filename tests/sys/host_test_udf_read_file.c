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

void test_udf_read_file_inline() {
    printf("Running test_udf_read_file_inline...\n");

    struct udf_fs fs;
    memset(&fs, 0, sizeof(fs));

    // Create a mock FE
    uint8_t buffer[2048];
    memset(buffer, 0, sizeof(buffer));
    struct udf_fe *fe = (struct udf_fe *)buffer;

    fe->icb_tag.flags = UDF_ICB_FLAG_AD_INLINE;
    fe->ext_attr_length = 0;

    const char *test_data = "Hello, UDF inline data!";
    uint32_t data_len = strlen(test_data);
    fe->info_length = data_len;

    uint8_t *alloc_area = buffer + sizeof(struct udf_fe);
    memcpy(alloc_area, test_data, data_len);

    uint8_t read_buf[100];

    // Read whole
    memset(read_buf, 0, sizeof(read_buf));
    uint32_t read_bytes = udf_read_file(&fs, fe, 0, data_len, read_buf);
    assert(read_bytes == data_len);
    assert(memcmp(read_buf, test_data, data_len) == 0);

    // Read partial from middle
    memset(read_buf, 0, sizeof(read_buf));
    read_bytes = udf_read_file(&fs, fe, 7, 3, read_buf); // "UDF"
    assert(read_bytes == 3);
    assert(memcmp(read_buf, "UDF", 3) == 0);

    // Read out of bounds
    memset(read_buf, 0, sizeof(read_buf));
    read_bytes = udf_read_file(&fs, fe, data_len, 10, read_buf);
    assert(read_bytes == 0);

    // Read overlapping bounds
    memset(read_buf, 0, sizeof(read_buf));
    read_bytes = udf_read_file(&fs, fe, data_len - 5, 10, read_buf);
    assert(read_bytes == 5);
    assert(memcmp(read_buf, "data!", 5) == 0);

    printf("test_udf_read_file_inline PASSED\n");
}

void test_udf_read_file_short_ad() {
    printf("Running test_udf_read_file_short_ad...\n");
    setup_mock_disk(100);

    fs_node_t dev;
    dev.read = mock_read;

    struct udf_fs fs;
    memset(&fs, 0, sizeof(fs));
    fs.device = &dev;
    fs.partition_start = 10;

    // Setup first extent at partition sector 5 (disk sector 15)
    uint8_t ext1_data[UDF_SECTOR_SIZE];
    memset(ext1_data, 'A', UDF_SECTOR_SIZE);
    write_sector(15, ext1_data, UDF_SECTOR_SIZE);

    // Setup second extent at partition sector 6 (disk sector 16)
    uint8_t ext2_data[UDF_SECTOR_SIZE];
    memset(ext2_data, 'B', UDF_SECTOR_SIZE);
    write_sector(16, ext2_data, UDF_SECTOR_SIZE);

    // Create a mock FE with short ADs
    uint8_t buffer[2048];
    memset(buffer, 0, sizeof(buffer));
    struct udf_fe *fe = (struct udf_fe *)buffer;

    fe->icb_tag.flags = UDF_ICB_FLAG_AD_SHORT;
    fe->ext_attr_length = 0;
    fe->alloc_desc_length = 2 * sizeof(struct udf_short_ad);
    fe->info_length = 2 * UDF_SECTOR_SIZE;

    struct udf_short_ad *ads = (struct udf_short_ad *)(buffer + sizeof(struct udf_fe));
    ads[0].position = 5;
    ads[0].length = UDF_SECTOR_SIZE; // type 0
    ads[1].position = 6;
    ads[1].length = UDF_SECTOR_SIZE; // type 0

    uint8_t read_buf[UDF_SECTOR_SIZE * 3];

    // Read from first extent
    memset(read_buf, 0, sizeof(read_buf));
    uint32_t read_bytes = udf_read_file(&fs, fe, 0, 10, read_buf);
    assert(read_bytes == 10);
    for (int i = 0; i < 10; i++) assert(read_buf[i] == 'A');

    // Read spanning both extents
    memset(read_buf, 0, sizeof(read_buf));
    read_bytes = udf_read_file(&fs, fe, UDF_SECTOR_SIZE - 5, 10, read_buf);
    assert(read_bytes == 10);
    for (int i = 0; i < 5; i++) assert(read_buf[i] == 'A');
    for (int i = 5; i < 10; i++) assert(read_buf[i] == 'B');

    // Read unrecorded extent (type 1)
    ads[1].length = (1 << 30) | UDF_SECTOR_SIZE; // type 1
    memset(read_buf, 0xFF, sizeof(read_buf));
    read_bytes = udf_read_file(&fs, fe, UDF_SECTOR_SIZE, 10, read_buf);
    assert(read_bytes == 10);
    for (int i = 0; i < 10; i++) assert(read_buf[i] == 0); // should be zero-filled

    teardown_mock_disk();
    printf("test_udf_read_file_short_ad PASSED\n");
}

int main() {
    test_udf_read_file_inline();
    test_udf_read_file_short_ad();

    printf("\nAll udf_read_file tests PASSED!\n");
    return 0;
}
