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
