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
#include <sys/lock.h>

void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

// Missing mocks
void mutex_lock(mutex_t *m) { (void)m; }
void mutex_unlock(mutex_t *m) { (void)m; }
void mutex_init(mutex_t *m, const char *name) { (void)m; (void)name; }

#include <vm/uma.h>
void *uma_zalloc(uma_zone_t *zone, int flags) { (void)zone; (void)flags; return calloc(1, 4096); }
void uma_zfree(uma_zone_t *zone, void *item) { (void)zone; free(item); }
uma_zone_t *uma_zcreate(
    const char *name, size_t size,
    int (*ctor)(void *mem, int size, void *arg, int flags),
    void (*dtor)(void *mem, int size, void *arg),
    int (*init)(void *mem, int size, int flags),
    void (*fini)(void *mem, int size),
    int align, uint32_t flags)
{
    (void)name; (void)size; (void)ctor; (void)dtor; (void)init; (void)fini; (void)align; (void)flags; return NULL;
}

// Mock Device Implementation
static uint8_t mock_disk[4096 * 10]; // Small disk for testing

size_t mock_device_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    if (offset + size > sizeof(mock_disk)) return 0;
    memcpy(buffer, mock_disk + offset, size);
    return size;
}

size_t mock_device_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node;
    if (offset + size > sizeof(mock_disk)) return 0;
    memcpy(mock_disk + offset, buffer, size);
    return size;
}

int64_t get_time(void) {
    return 0;
}

// Include Driver Source
#define vasprintf kernel_vasprintf
#include "../../sys/fs/ext2/ext2.c"

int main() {
    printf("Running Ext2 Alloc Inode Block Tests...\n");
    int failed = 0;

    // 1. Setup Filesystem
    ext2_fs_t fs;
    memset(&fs, 0, sizeof(fs));

    fs_node_t device_node;
    memset(&device_node, 0, sizeof(device_node));
    device_node.read = mock_device_read;
    device_node.write = mock_device_write;

    fs.device = &device_node;
    fs.block_size = 1024;
    fs.inodes_per_group = 100;
    fs.blocks_per_group = 10000;

    // Fix: We need to set s_free_blocks_count and s_first_data_block to non-zero values
    fs.sb.s_free_blocks_count = 1000;
    fs.sb.s_first_data_block = 1;

    // Setup simple block group descriptors
    ext2_group_desc_t bgd[1];
    memset(bgd, 0, sizeof(bgd));
    bgd[0].bg_free_blocks_count = 1000;
    bgd[0].bg_block_bitmap = 1; // Block 1 holds the bitmap
    fs.bgd = bgd;
    fs.group_count = 1;

    // Create an active block bitmap filled with 0s (free blocks)
    uint8_t active_bitmap[1024];
    memset(active_bitmap, 0, sizeof(active_bitmap));
    fs.active_bg_bitmap = active_bitmap;
    fs.active_bg_group = 0;

    // Also write it to the mock disk at block 1
    memcpy(mock_disk + 1024, active_bitmap, 1024);

    // 2. Setup Inode
    ext2_inode_t inode;
    memset(&inode, 0, sizeof(inode));

    uint32_t indirect_buf[256]; // 1024 bytes / 4 bytes per ptr
    memset(indirect_buf, 0, sizeof(indirect_buf));

    // Test 1: Direct block allocation (block_idx < 12)
    printf("Test 1: Allocating direct block 5\n");
    int ret = ext2_alloc_inode_block(&fs, &inode, 5, indirect_buf);
    if (ret != 0) {
        printf("FAILED: ext2_alloc_inode_block returned %d instead of 0\n", ret);
        failed++;
    } else if (inode.i_block[5] == 0) {
        printf("FAILED: inode.i_block[5] was not set\n");
        failed++;
    } else {
        printf("SUCCESS: Direct block allocated at block %d\n", inode.i_block[5]);
    }

    // Test 2: Indirect block allocation (block_idx >= 12, indirect block empty)
    printf("Test 2: Allocating indirect block 12 (first indirect pointer)\n");
    // Before this, i_block[12] is 0
    ret = ext2_alloc_inode_block(&fs, &inode, 12, indirect_buf);
    if (ret != 0) {
        printf("FAILED: ext2_alloc_inode_block returned %d instead of 0\n", ret);
        failed++;
    } else if (inode.i_block[12] == 0) {
        printf("FAILED: inode.i_block[12] (indirect block) was not set\n");
        failed++;
    } else if (indirect_buf[0] == 0) {
        printf("FAILED: indirect_buf[0] was not set\n");
        failed++;
    } else {
        printf("SUCCESS: Indirect block allocated. Indirect block table at %d, data block at %d\n",
               inode.i_block[12], indirect_buf[0]);
    }

    // Test 3: Indirect block allocation (block_idx > 12, indirect block already allocated)
    printf("Test 3: Allocating indirect block 13 (second indirect pointer)\n");
    ret = ext2_alloc_inode_block(&fs, &inode, 13, indirect_buf);
    if (ret != 0) {
        printf("FAILED: ext2_alloc_inode_block returned %d instead of 0\n", ret);
        failed++;
    } else if (indirect_buf[1] == 0) {
        printf("FAILED: indirect_buf[1] was not set\n");
        failed++;
    } else {
        printf("SUCCESS: Indirect data block allocated at %d\n", indirect_buf[1]);
    }

    // Test 4: Double indirect block (not implemented, should return -1)
    printf("Test 4: Allocating double indirect block (should fail as unsupported)\n");
    uint32_t ptrs_per_block = fs.block_size / 4;
    ret = ext2_alloc_inode_block(&fs, &inode, 12 + ptrs_per_block, indirect_buf);
    if (ret != -1) {
        printf("FAILED: ext2_alloc_inode_block returned %d instead of -1 for double indirect\n", ret);
        failed++;
    } else {
        printf("SUCCESS: Double indirect properly returned error\n");
    }

    // Test 5: Allocation failure (no free blocks)
    printf("Test 5: Allocating when no free blocks\n");
    // Make sure no more free blocks are reported.
    // We also set the bitmap to all 1s and write it to disk.
    bgd[0].bg_free_blocks_count = 0;
    fs.sb.s_free_blocks_count = 0;
    memset(active_bitmap, 0xFF, sizeof(active_bitmap));
    memcpy(mock_disk + 1024, active_bitmap, 1024);

    ret = ext2_alloc_inode_block(&fs, &inode, 6, indirect_buf);
    if (ret != -1) {
        printf("FAILED: ext2_alloc_inode_block returned %d instead of -1 when no space\n", ret);
        failed++;
    } else {
        printf("SUCCESS: Out of space properly handled\n");
    }

    if (failed == 0) {
        printf("All tests PASSED!\n");
        return 0;
    } else {
        printf("%d tests FAILED!\n", failed);
        return 1;
    }
}
