#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Mocking dependencies

// Mock kprint
void kprint(const char *str) {
    printf("%s", str);
}

// Mock kmalloc/kfree
#include <vm/vm_kmem.h>
void *kmalloc(size_t size) {
    return calloc(1, size);
}
void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

// Mock uma_zalloc/uma_zfree
#include <vm/uma.h>
void *uma_zalloc(uma_zone_t *zone, int flags) {
    return malloc(4096);
}
void uma_zfree(uma_zone_t *zone, void *item) {
    free(item);
}

// Mock vfs
#include <vfs/vfs.h>
void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

// Mock get_time
int64_t get_time(void) { return 0; }

// Mock uma_zcreate
uma_zone_t *uma_zcreate(
    const char *name, size_t size,
    int (*ctor)(void *, int, void *, int),
    void (*dtor)(void *, int, void *),
    int (*init)(void *, int, int),
    void (*fini)(void *, int),
    int align, uint32_t flags) {
    (void)name; (void)size; (void)ctor; (void)dtor; (void)init; (void)fini; (void)align; (void)flags;
    return (uma_zone_t*)1;
}

// Mock mutexes (implementing sys/lock.h declarations)
#include <sys/lock.h>
void mutex_init(mutex_t *m, const char *name) { (void)m; (void)name; }
void mutex_lock(mutex_t *m) { (void)m; }
void mutex_unlock(mutex_t *m) { (void)m; }

// ==========================================
// Stubs for kernel subsystems ext2.c grew calls into
// ==========================================
int kprintf(const char *fmt, ...) { (void)fmt; return 0; }
int cmdline_debug_enabled(const char *channel) { (void)channel; return 0; }

#include <crc32c.h>
uint32_t crc32c_update(uint32_t crc, const void *buf, size_t len) {
    (void)buf; (void)len; return crc;
}

#include <drivers/storage/blkdev.h>
size_t blkdev_read_bytes(blkdev_t *dev, uint64_t offset, size_t size, void *buffer) {
    (void)dev; (void)offset; (void)size; (void)buffer; return 0;
}

#include <fs/ext2/ext2.h>
int ext2_htree_hash(const char *name, int len, const uint32_t *hash_seed,
                    int hash_version, uint32_t *hash_major, uint32_t *hash_minor) {
    (void)name; (void)len; (void)hash_seed; (void)hash_version;
    if (hash_major) *hash_major = 0;
    if (hash_minor) *hash_minor = 0;
    return 0;
}
int ext2_xattr_get(fs_node_t *node, const char *full_name,
                   void *out, size_t out_size, size_t *result_size) {
    (void)node; (void)full_name; (void)out; (void)out_size;
    if (result_size) *result_size = 0;
    return -1; /* miss; xattr path is not exercised by this test */
}
int ext2_xattr_list(fs_node_t *node, void *out, size_t out_size, size_t *result_size) {
    (void)node; (void)out; (void)out_size;
    if (result_size) *result_size = 0;
    return 0;
}

unsigned long fs_open_count;
unsigned long fs_close_count;

// Device Mock
size_t mock_device_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node; (void)offset; (void)size;
    memset(buffer, 0, size);
    return size;
}

size_t mock_device_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    return size;
}

#include "../../sys/fs/ext2/ext2.c"

int main() {
    printf("Testing ext2_free_block...\n");

    ext2_fs_t fs;
    memset(&fs, 0, sizeof(fs));

    fs_node_t dev;
    memset(&dev, 0, sizeof(dev));
    dev.read = mock_device_read;
    dev.write = mock_device_write;

    fs.device = &dev;
    fs.block_size = 1024;
    fs.sb.s_first_data_block = 1;
    fs.blocks_per_group = 8192;
    fs.group_count = 1;

    ext2_group_desc_t bgd[1];
    memset(bgd, 0, sizeof(bgd));
    bgd[0].bg_block_bitmap = 2; // Arbitrary block
    fs.bgd = bgd;

    // Allocate buffer for active bitmap
    fs.active_bg_bitmap = malloc(fs.block_size);
    memset(fs.active_bg_bitmap, 0xFF, fs.block_size); // all blocks used initially
    fs.active_bg_group = 0;

    // Test freeing block 1 (first data block, index 0 in bitmap)
    uint32_t block_to_free = 1;
    ext2_free_block(&fs, block_to_free);

    int failures = 0;

    // Verify it was freed
    uint8_t *bitmap = fs.active_bg_bitmap;
    if ((bitmap[0] & 1) == 0) {
        printf("PASS: Block %u successfully freed in bitmap.\n", block_to_free);
    } else {
        printf("FAIL: Block %u not freed in bitmap.\n", block_to_free);
        failures++;
    }

    if (fs.bgd[0].bg_free_blocks_count == 1) {
        printf("PASS: bg_free_blocks_count incremented.\n");
    } else {
        printf("FAIL: bg_free_blocks_count not incremented (is %u).\n", fs.bgd[0].bg_free_blocks_count);
        failures++;
    }

    if (fs.sb.s_free_blocks_count == 1) {
        printf("PASS: s_free_blocks_count incremented.\n");
    } else {
        printf("FAIL: s_free_blocks_count not incremented (is %u).\n", fs.sb.s_free_blocks_count);
        failures++;
    }

    // Edge Cases
    // Free block out of range
    ext2_free_block(&fs, 99999);
    printf("PASS: Handled out-of-range block (didn't crash).\n");

    // NULL fs
    ext2_free_block(NULL, 1);
    printf("PASS: Handled NULL fs.\n");

    // Block 0 (invalid)
    ext2_free_block(&fs, 0);
    printf("PASS: Handled block 0.\n");

    free(fs.active_bg_bitmap);
    return failures > 0 ? 1 : 0;
}
