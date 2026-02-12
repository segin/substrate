#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

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

// Mock VFS functions
void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

// Mock get_time
int64_t get_time(void) {
    return 0;
}

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

uint64_t rdtsc_host() {
    unsigned int lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

void run_ext2_perf_test_host(void) {
    printf("TEST: Benchmarking Ext2 Block Allocation (Host)...\n");

    // Initialize mock disk with zeros
    memset(mock_disk, 0, sizeof(mock_disk));

    // --- Setup Filesystem Structures on "Disk" ---
    ext2_superblock_t *sb = (ext2_superblock_t *)(mock_disk + 1024);
    sb->s_magic = EXT2_SUPER_MAGIC;
    sb->s_blocks_count = BLOCKS_COUNT;
    sb->s_log_block_size = 0; // 1024 bytes
    sb->s_blocks_per_group = BLOCKS_COUNT; // All in one group
    sb->s_inodes_per_group = 1024;
    sb->s_first_data_block = 1;
    sb->s_rev_level = 1;
    sb->s_free_blocks_count = BLOCKS_COUNT - 200;

    ext2_group_desc_t *bgd_disk = (ext2_group_desc_t *)(mock_disk + 2048);
    bgd_disk->bg_block_bitmap = 3;
    bgd_disk->bg_inode_bitmap = 4;
    bgd_disk->bg_inode_table = 5;
    bgd_disk->bg_free_blocks_count = BLOCKS_COUNT - 200;

    uint8_t *bitmap = mock_disk + 3072;
    memset(bitmap, 0xFF, 25); // 25 bytes = 200 bits

    // --- Setup In-Memory FS Structure ---
    ext2_fs_t fs;
    memset(&fs, 0, sizeof(fs));

    fs_node_t dev_node;
    memset(&dev_node, 0, sizeof(dev_node));
    dev_node.read = mock_read;
    dev_node.write = mock_write;

    fs.device = &dev_node;
    fs.sb = *sb;
    fs.block_size = 1024;
    fs.group_count = 1;
    fs.blocks_per_group = BLOCKS_COUNT;

    ext2_group_desc_t bgd_table[1];
    bgd_table[0] = *bgd_disk;
    fs.bgd = bgd_table;

    // --- Benchmark Loop ---
    uint64_t start_tsc, end_tsc;
    uint32_t allocated_count = 0;
    uint32_t blocks_to_alloc = 5000;
    static uint32_t allocated_blocks[8192];
    memset(allocated_blocks, 0, sizeof(allocated_blocks));

    start_tsc = rdtsc_host();

    for (uint32_t i = 0; i < blocks_to_alloc; i++) {
        uint32_t block = ext2_alloc_block(&fs);
        if (block != 0) {
            allocated_count++;
            // Check for duplicates
            if (block < 8192) {
                if (allocated_blocks[block]) {
                    printf("ERROR: Duplicate block allocated: %d\n", block);
                }
                allocated_blocks[block] = 1;
            }
        } else {
            printf("Failed to allocate block at iteration %d\n", i);
            break;
        }
    }

    end_tsc = rdtsc_host();

    if (allocated_count > 0) {
        uint64_t total_cycles = end_tsc - start_tsc;
        printf("EXT2 Alloc Perf: %u blocks in %lu cycles (approx)\n", allocated_count, total_cycles);
        printf("Average cycles per block: %lu\n", total_cycles / allocated_count);
    } else {
        printf("EXT2 Alloc Perf: No blocks allocated.\n");
    }
}

int main() {
    run_ext2_perf_test_host();
    return 0;
}
