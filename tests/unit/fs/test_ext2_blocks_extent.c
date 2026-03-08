#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#ifndef HOST_TEST
#define HOST_TEST
#endif

#include <sys/types.h>
#include <vfs/vfs.h>
#include <fs/ext2/ext2.h>

// Prevent duplicate definitions if we include it
#define vasprintf kernel_vasprintf

#include "../../../sys/fs/ext2/ext2.c"

// ------------------------------------------------------------------
// Mock Environment
// ------------------------------------------------------------------
// We need a mock block read function to populate indirect blocks
static uint32_t mock_blocks[1024 * 1024]; // Large enough block array

static size_t mock_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    uint32_t block_idx = offset / 1024; // Assuming 1024 block size
    if (size != 1024) return 0; // Only supporting block-aligned reads

    // Copy 1024 bytes from our mock_blocks
    memcpy(buffer, &mock_blocks[block_idx * (1024 / sizeof(uint32_t))], size);
    return size;
}

// ------------------------------------------------------------------
// Tests
// ------------------------------------------------------------------

bool test_ext2_blocks_extent(void) {
    ext2_fs_t fs;
    memset(&fs, 0, sizeof(fs));
    fs.block_size = 1024;

    fs_node_t dev_node;
    memset(&dev_node, 0, sizeof(dev_node));
    dev_node.read = mock_read;
    fs.device = &dev_node;

    ext2_inode_t inode;
    memset(&inode, 0, sizeof(inode));

    uint32_t phys_block = 0;
    uint32_t count = 0;
    uint32_t indirect_buf[256];
    uint32_t dindirect_buf[256];
    uint32_t tindirect_buf[256];

    memset(mock_blocks, 0, sizeof(mock_blocks));

    // Test 1: max_count == 0
    ext2_get_blocks_extent(&fs, &inode, 0, 0, &phys_block, &count, indirect_buf, dindirect_buf, tindirect_buf);
    if (count != 0 || phys_block != 0) {
        printf("Test 1 Failed: Expected 0 blocks, got %u\n", count);
        return false;
    }

    // Test 2: Contiguous direct blocks
    inode.i_block[0] = 10;
    inode.i_block[1] = 11;
    inode.i_block[2] = 12;
    inode.i_block[3] = 14; // Non-contiguous

    ext2_get_blocks_extent(&fs, &inode, 0, 4, &phys_block, &count, indirect_buf, dindirect_buf, tindirect_buf);
    if (phys_block != 10 || count != 3) {
        printf("Test 2 Failed: Expected phys=10, count=3, got phys=%u, count=%u\n", phys_block, count);
        return false;
    }

    // Test 3: Sparse direct blocks
    inode.i_block[0] = 0;
    inode.i_block[1] = 0;
    inode.i_block[2] = 12;

    ext2_get_blocks_extent(&fs, &inode, 0, 4, &phys_block, &count, indirect_buf, dindirect_buf, tindirect_buf);
    if (phys_block != 0 || count != 2) {
        printf("Test 3 Failed: Expected phys=0, count=2, got phys=%u, count=%u\n", phys_block, count);
        return false;
    }

    // Test 4: Indirect block contiguous runs
    inode.i_block[0] = 0;
    inode.i_block[1] = 0;
    inode.i_block[2] = 0;

    inode.i_block[12] = 100; // Indirect block at block 100
    // Populate mock indirect block
    uint32_t *ind_block = &mock_blocks[100 * (1024 / sizeof(uint32_t))];
    ind_block[0] = 200;
    ind_block[1] = 201;
    ind_block[2] = 203; // Non-contiguous

    ext2_get_blocks_extent(&fs, &inode, 12, 4, &phys_block, &count, indirect_buf, dindirect_buf, tindirect_buf);
    if (phys_block != 200 || count != 2) {
        printf("Test 4 Failed: Expected phys=200, count=2, got phys=%u, count=%u\n", phys_block, count);
        return false;
    }

    return true;
}

bool test_ext2_blocks_extent_run(void) {
    return test_ext2_blocks_extent();
}