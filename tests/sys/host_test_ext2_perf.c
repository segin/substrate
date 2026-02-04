#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// ==========================================
// Mocks for Kernel Environment
// ==========================================

// Mock Types
typedef long off_t;

// Mock kprint
void kprint(const char *str) {
    printf("%s", str);
}

// Mock kmalloc/kfree
#include <vm/vm_kmem.h>
void *kmalloc(size_t size) {
    return calloc(1, size); // calloc to zero memory
}
void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

// Mock VFS
#include <vfs/vfs.h>

void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

// Mock Device Read Tracking
static int device_read_count = 0;
static size_t device_bytes_read = 0;

// Mock Device Implementation
// We simulate a block device.
// Block 1000: Indirect Block (contains pointers 2000, 2001, ...)
// Blocks 2000-2255: Data Blocks
size_t mock_device_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    device_read_count++;
    device_bytes_read += size;

    // printf("DEBUG: Read device offset %ld size %lu\n", offset, size);

    memset(buffer, 0xAA, size); // Fill with dummy data

    // If reading the indirect block (Address 1000 * 1024 = 1024000)
    if (offset == 1024000 && size == 1024) {
        uint32_t *ptrs = (uint32_t *)buffer;
        for (int i = 0; i < 256; i++) {
            ptrs[i] = 2000 + i;
        }
    }

    return size;
}

size_t mock_device_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    return size;
}

// Mock Time
int64_t get_time(void) {
    return 0;
}

// ==========================================
// Include Driver Source
// ==========================================

// Map standard headers to prevent conflicts if needed
// The driver includes <fs/ext2/ext2.h>. We need to make sure it finds it.
// We will compile with -Isys/include -Isys

#define vasprintf kernel_vasprintf
#include "../../sys/fs/ext2/ext2.c"

// ==========================================
// Test Runner
// ==========================================

int main() {
    printf("Running Ext2 Performance Benchmark...\n");

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

    // 2. Setup Inode
    ext2_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.i_size = (12 + 256) * 1024; // 12 direct + 256 indirect blocks
    inode.i_mode = EXT2_S_IFREG;

    // Set Direct Blocks (0-11)
    for (int i = 0; i < 12; i++) {
        inode.i_block[i] = 100 + i;
    }
    // Set Indirect Block (12)
    inode.i_block[12] = 1000; // Physical block 1000

    // 3. Perform Read Test
    // Read 20 blocks from the indirect region.
    // Logical block 12 starts the indirect region.
    // Offset = 12 * 1024
    // Length = 20 * 1024

    off_t start_offset = 12 * 1024;
    size_t read_len = 20 * 1024;
    uint8_t *buffer = malloc(read_len);

    printf("Starting Read: Offset %ld, Size %lu\n", start_offset, read_len);

    device_read_count = 0;
    device_bytes_read = 0;

    uint32_t result = ext2_inode_read(&fs, &inode, start_offset, read_len, buffer);

    printf("Read Completed: %u bytes\n", result);
    printf("Device Read Calls: %d\n", device_read_count);
    printf("Device Bytes Read: %lu\n", device_bytes_read);

    // Analysis
    // Unoptimized:
    // 20 data blocks = 20 reads
    // For each data block, it reads the indirect block (1000) = 20 reads
    // Total = 40 reads
    // Expected optimized:
    // Indirect block read once (or cached)
    // Data blocks read in larger chunks (ideally 1 big read if contiguous)
    // If contiguous: 1 read for indirect, 1 read for data = 2 reads.
    // If just removing indirect redundancy but still 1-block at a time: 1 + 20 = 21 reads.

    if (device_read_count >= 40) {
        printf("RESULT: HIGH OVERHEAD (Baseline)\n");
    } else if (device_read_count <= 22) {
        printf("RESULT: LOW OVERHEAD (Optimized)\n");
    } else {
        printf("RESULT: MODERATE IMPROVEMENT\n");
    }

    free(buffer);
    return 0;
}
