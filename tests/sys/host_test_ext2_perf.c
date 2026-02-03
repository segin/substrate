#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

// Mock kernel functions
void kprint(const char *str) {
    printf("%s", str);
}

int64_t get_time(void) {
    return 1234567890;
}

struct filesystem;
void vfs_register_filesystem(struct filesystem *fs) {
    (void)fs;
}

// Performance counters
static uint64_t mock_read_count = 0;
static uint64_t mock_read_bytes = 0;

// Need to include headers for types used in ext2.c
// We will compile with -Isys so <vfs/vfs.h> etc work.

#include <vfs/vfs.h>

// Helper to reset counters
void reset_counters() {
    mock_read_count = 0;
    mock_read_bytes = 0;
}

// Mock device read function
static size_t mock_device_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    mock_read_count++;
    mock_read_bytes += size;
    // Fill with pattern
    memset(buffer, (offset & 0xFF), size);
    return size;
}

// Resolve conflict with host vasprintf
#define vasprintf kernel_vasprintf

// Include the source file under test
// This gives us access to internal types and static functions if needed
// And definitions of ext2_inode_read
#include "../../sys/fs/ext2/ext2.c"

// Note: ext2.c defines ext2_fs static variable, but we might want to use our own or manipulate it.
// ext2_inode_read takes fs pointer.

int main(int argc, char **argv) {
    printf("Running Ext2 Performance Benchmark\n");

    // Setup Mock Filesystem Context
    ext2_fs_t fs;
    memset(&fs, 0, sizeof(fs));

    // Setup Mock Device
    fs_node_t device;
    memset(&device, 0, sizeof(device));
    device.read = mock_device_read;
    fs.device = &device;

    // Setup Block Size (1KB)
    fs.block_size = 1024;
    fs.inodes_per_group = 16;
    fs.blocks_per_group = 8192;
    fs.group_count = 1;

    // Setup Mock Inode
    ext2_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.i_size = 16384; // 16KB

    // Set direct blocks 0-11 to be contiguous: 100, 101, 102...
    for (int i = 0; i < 12; i++) {
        inode.i_block[i] = 100 + i;
    }

    // Buffer for reading
    uint8_t *buffer = malloc(16384);

    // TEST 1: Read 4KB (4 blocks) contiguous
    printf("\nTest 1: Read 4KB (4 blocks) contiguous\n");
    reset_counters();

    uint32_t read_size = 4096;
    ext2_inode_read(&fs, &inode, 0, read_size, buffer);

    printf("Read %u bytes. Counters: %lu calls, %lu bytes\n",
           read_size, mock_read_count, mock_read_bytes);

    // Baseline expectation: 4 calls (one per block)
    // After optimization: 1 call

    // TEST 2: Read 10KB (10 blocks) contiguous
    printf("\nTest 2: Read 10KB (10 blocks) contiguous\n");
    reset_counters();

    read_size = 10240;
    ext2_inode_read(&fs, &inode, 0, read_size, buffer);

    printf("Read %u bytes. Counters: %lu calls, %lu bytes\n",
           read_size, mock_read_count, mock_read_bytes);

    // TEST 3: Read with hole
    // Set block 5 to 0 (hole)
    printf("\nTest 3: Read 8KB with hole at block 5\n");
    inode.i_block[5] = 0;
    reset_counters();

    read_size = 8192;
    ext2_inode_read(&fs, &inode, 0, read_size, buffer);

    printf("Read %u bytes. Counters: %lu calls, %lu bytes\n",
           read_size, mock_read_count, mock_read_bytes);

    // Expectation:
    // Baseline: 7 calls (read) + 1 hole (memset) = 7 reads
    // Optimized: Read 0-4 (5 blocks), Skip 5, Read 6-7 (2 blocks) = 2 reads

    free(buffer);
    return 0;
}
