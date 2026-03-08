#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
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
    uma_zone_t *zone = (uma_zone_t *)calloc(1, sizeof(uma_zone_t));
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
#include <fs/ext2/ext2.c>

// ------------------------------------------------------------------
// Test Logic
// ------------------------------------------------------------------

#define MOCK_DISK_SIZE (1024 * 1024) // 1MB disk
static uint8_t mock_disk[MOCK_DISK_SIZE];

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
    return size;
}

void test_ext2_read_blocks() {
    printf("TEST: ext2_read_blocks()\n");

    // Init mock disk with pattern
    for (int i = 0; i < MOCK_DISK_SIZE; i++) {
        mock_disk[i] = (uint8_t)(i % 256);
    }

    ext2_fs_t fs;
    memset(&fs, 0, sizeof(fs));

    fs_node_t dev_node;
    memset(&dev_node, 0, sizeof(dev_node));
    dev_node.read = mock_read;
    dev_node.write = mock_write;

    fs.device = &dev_node;
    fs.block_size = 1024;

    uint8_t buffer[4096]; // 4 blocks of size 1024

    // Test Case 1: fs is NULL
    uint32_t ret = ext2_read_blocks(NULL, 0, 1, buffer);
    assert(ret == 0);
    printf("  [PASS] fs is NULL\n");

    // Test Case 2: fs->device is NULL
    fs.device = NULL;
    ret = ext2_read_blocks(&fs, 0, 1, buffer);
    assert(ret == 0);
    printf("  [PASS] fs->device is NULL\n");

    // Test Case 3: fs->device->read is NULL
    fs.device = &dev_node;
    dev_node.read = NULL;
    ret = ext2_read_blocks(&fs, 0, 1, buffer);
    assert(ret == 0);
    printf("  [PASS] fs->device->read is NULL\n");

    // Test Case 4: Read 0 blocks (should read 0 bytes)
    dev_node.read = mock_read;
    ret = ext2_read_blocks(&fs, 10, 0, buffer);
    assert(ret == 0);
    printf("  [PASS] Read 0 blocks\n");

    // Test Case 5: Read multiple blocks correctly
    // Read 3 blocks starting at block_num 5 (offset 5 * 1024 = 5120)
    memset(buffer, 0, sizeof(buffer));
    ret = ext2_read_blocks(&fs, 5, 3, buffer);

    // We expect 3 * 1024 = 3072 bytes read
    assert(ret == 3072);

    // Check if the content is correct
    bool content_correct = true;
    for (int i = 0; i < 3072; i++) {
        if (buffer[i] != (uint8_t)((5120 + i) % 256)) {
            content_correct = false;
            break;
        }
    }
    assert(content_correct);
    printf("  [PASS] Read multiple blocks correctly\n");

    printf("ALL TESTS PASSED: ext2_read_blocks()\n");
}

int main() {
    test_ext2_read_blocks();
    return 0;
}
