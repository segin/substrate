#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

// Define HOST_TEST to enable any conditional logic in kernel headers
#ifndef HOST_TEST
#define HOST_TEST
#endif

// Needed types
#include <sys/types.h>
#include <vfs/vfs.h>

// Mock Kernel Functions
void kprint(const char *msg) {
    printf("[KERNEL] %s", msg);
}

void *kmalloc(size_t size) {
    return calloc(1, size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

// Mock Mutex
#include <sys/lock.h>
void mutex_init(mutex_t *m, const char *name) { (void)m; (void)name; }
void mutex_lock(mutex_t *m) { (void)m; }
void mutex_unlock(mutex_t *m) { (void)m; }

// Mock VFS functions
void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

// Mock get_time
int64_t get_time(void) {
    return 0;
}

// UMA Mocks for Ext2
#include <vm/uma.h>

uma_zone_t *uma_zcreate(const char *name, size_t size, uma_ctor ctor, uma_dtor dtor, uma_init init, uma_fini fini, int align, uint32_t flags) {
    (void)ctor; (void)dtor; (void)init; (void)fini; (void)align; (void)flags;
    uma_zone_t *zone = (uma_zone_t *)calloc(1, sizeof(uma_zone_t)); // use calloc to avoid uninit
    if (zone) {
        zone->uz_name = name;
        zone->uz_size = size;
        zone->uz_flags = flags;
    }
    return zone;
}

void *uma_zalloc(uma_zone_t *zone, int flags) {
    (void)flags;
    if (!zone) return NULL;
    return calloc(1, zone->uz_size);
}

void uma_zfree(uma_zone_t *zone, void *item) {
    (void)zone;
    free(item);
}

void uma_zone_set_max(uma_zone_t *zone, int max) { (void)zone; (void)max; }

// Rename colliding kernel function
#define vasprintf kernel_vasprintf

// Include the source under test
// This is relative to tests/unit/fs/
#include <fs/ext2/ext2.c>

// ------------------------------------------------------------------
// Test Logic
// ------------------------------------------------------------------

#define BLOCKS_COUNT 8192
static uint8_t mock_disk[BLOCKS_COUNT * 1024];
static size_t write_called_count = 0;
static off_t last_write_offset = 0;
static size_t last_write_size = 0;

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
    write_called_count++;
    last_write_offset = offset;
    last_write_size = size;
    return size;
}

void run_ext2_write_block_test(void) {
    printf("TEST: ext2_write_block\n");

    // Initialize mock disk with zeros
    memset(mock_disk, 0, sizeof(mock_disk));

    // --- Setup In-Memory FS Structure ---
    ext2_fs_t fs;
    memset(&fs, 0, sizeof(fs));

    fs_node_t dev_node;
    memset(&dev_node, 0, sizeof(dev_node));
    dev_node.read = mock_read;
    dev_node.write = mock_write;

    fs.device = &dev_node;
    fs.block_size = 1024;
    fs.group_count = 1;
    fs.blocks_per_group = BLOCKS_COUNT;

    uint8_t buffer[1024];
    memset(buffer, 0xAB, sizeof(buffer));

    // Test 1: Write a valid block
    write_called_count = 0;
    uint32_t result = ext2_write_block(&fs, 5, buffer);

    assert(result == 1024);
    assert(write_called_count == 1);
    assert(last_write_offset == 5 * 1024);
    assert(last_write_size == 1024);
    assert(mock_disk[5 * 1024] == 0xAB);

    // Test 2: Null fs
    write_called_count = 0;
    result = ext2_write_block(NULL, 5, buffer);
    assert(result == 0);
    assert(write_called_count == 0);

    // Test 3: Null device
    fs.device = NULL;
    write_called_count = 0;
    result = ext2_write_block(&fs, 5, buffer);
    assert(result == 0);
    assert(write_called_count == 0);

    // Test 4: Null write function
    dev_node.write = NULL;
    fs.device = &dev_node;
    write_called_count = 0;
    result = ext2_write_block(&fs, 5, buffer);
    assert(result == 0);
    assert(write_called_count == 0);

    printf("ALL TESTS PASSED\n");
}

int main() {
    run_ext2_write_block_test();
    return 0;
}
