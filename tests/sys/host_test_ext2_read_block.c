#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// ==========================================
// Mocks for Kernel Environment
// ==========================================

typedef long off_t;

void kprint(const char *str) {
    (void)str;
}

#include <vm/vm_kmem.h>
void *kmalloc(size_t size) {
    return calloc(1, size);
}
void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

#include <vfs/vfs.h>

void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

static size_t mock_device_read_calls = 0;
static off_t last_mock_device_read_offset = 0;
static size_t last_mock_device_read_size = 0;
static uint8_t mock_read_data[4096];

size_t mock_device_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    mock_device_read_calls++;
    last_mock_device_read_offset = offset;
    last_mock_device_read_size = size;

    if (size <= sizeof(mock_read_data)) {
        memcpy(buffer, mock_read_data, size);
        return size;
    }
    return 0;
}

size_t mock_device_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    return size;
}

int64_t get_time(void) {
    return 0;
}

// Include Driver Source
#define vasprintf kernel_vasprintf
#include "../../sys/fs/ext2/ext2.c"

int main() {
    printf("Running ext2_read_block tests...\n");

    ext2_fs_t fs;
    memset(&fs, 0, sizeof(fs));

    fs_node_t device_node;
    memset(&device_node, 0, sizeof(device_node));
    device_node.read = mock_device_read;
    device_node.write = mock_device_write;

    fs.device = &device_node;
    fs.block_size = 1024;

    uint8_t buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    memset(mock_read_data, 0xAA, sizeof(mock_read_data));

    // Test 1: Successful read
    mock_device_read_calls = 0;
    uint32_t result = ext2_read_block(&fs, 5, buffer);

    if (result != 1024) {
        printf("FAILED Test 1: Expected return 1024, got %u\n", result);
        return 1;
    }
    if (mock_device_read_calls != 1) {
        printf("FAILED Test 1: Expected 1 device read call, got %zu\n", mock_device_read_calls);
        return 1;
    }
    if (last_mock_device_read_offset != 5 * 1024) {
        printf("FAILED Test 1: Expected offset %u, got %ld\n", 5 * 1024, last_mock_device_read_offset);
        return 1;
    }
    if (last_mock_device_read_size != 1024) {
        printf("FAILED Test 1: Expected size 1024, got %zu\n", last_mock_device_read_size);
        return 1;
    }
    if (buffer[0] != 0xAA) {
        printf("FAILED Test 1: Buffer not populated correctly\n");
        return 1;
    }

    // Test 2: Null fs
    mock_device_read_calls = 0;
    result = ext2_read_block(NULL, 5, buffer);
    if (result != 0) {
        printf("FAILED Test 2: Expected return 0 for NULL fs, got %u\n", result);
        return 1;
    }
    if (mock_device_read_calls != 0) {
        printf("FAILED Test 2: Expected 0 device read calls, got %zu\n", mock_device_read_calls);
        return 1;
    }

    // Test 3: Null device
    fs.device = NULL;
    mock_device_read_calls = 0;
    result = ext2_read_block(&fs, 5, buffer);
    if (result != 0) {
        printf("FAILED Test 3: Expected return 0 for NULL device, got %u\n", result);
        return 1;
    }
    if (mock_device_read_calls != 0) {
        printf("FAILED Test 3: Expected 0 device read calls, got %zu\n", mock_device_read_calls);
        return 1;
    }

    // Test 4: Null device read
    fs.device = &device_node;
    device_node.read = NULL;
    mock_device_read_calls = 0;
    result = ext2_read_block(&fs, 5, buffer);
    if (result != 0) {
        printf("FAILED Test 4: Expected return 0 for NULL device read, got %u\n", result);
        return 1;
    }
    if (mock_device_read_calls != 0) {
        printf("FAILED Test 4: Expected 0 device read calls, got %zu\n", mock_device_read_calls);
        return 1;
    }

    printf("All ext2_read_block tests passed!\n");
    return 0;
}

// Mocks for UMA and Mutex
void *uma_zalloc(uma_zone_t *zone, int flags) { (void)zone; (void)flags; return calloc(1, 4096); }
void uma_zfree(uma_zone_t *zone, void *item) { (void)zone; free(item); }
uma_zone_t *uma_zcreate(const char *name, size_t size, uma_ctor ctor, uma_dtor dtor, uma_init uinit, uma_fini fini, int align, uint32_t flags) {
    (void)name; (void)size; (void)ctor; (void)dtor; (void)uinit; (void)fini; (void)align; (void)flags;
    return (void*)1;
}

void mutex_init(mutex_t *mutex, const char *name) { (void)mutex; }
void mutex_lock(mutex_t *mutex) { (void)mutex; }
void mutex_unlock(mutex_t *mutex) { (void)mutex; }
