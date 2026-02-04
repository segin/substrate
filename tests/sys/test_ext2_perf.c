#include <kern/console.h>
#include <fs/ext2/ext2.h>
#include <string.h>
#include "tests.h"

// Forward declaration of the function we want to benchmark
extern uint32_t ext2_alloc_block(ext2_fs_t *fs);

// Mock disk parameters
#define BLOCK_SIZE 1024
#define BLOCKS_COUNT 8192
static uint8_t mock_disk[BLOCKS_COUNT * BLOCK_SIZE];

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

void run_ext2_perf_test(void) {
    kprint("TEST: Benchmarking Ext2 Block Allocation...\n");

    // Initialize mock disk with zeros
    memset(mock_disk, 0, sizeof(mock_disk));

    // --- Setup Filesystem Structures on "Disk" ---

    // 1. Superblock at offset 1024
    ext2_superblock_t *sb = (ext2_superblock_t *)(mock_disk + 1024);
    sb->s_magic = EXT2_SUPER_MAGIC;
    sb->s_blocks_count = BLOCKS_COUNT;
    sb->s_log_block_size = 0; // 1024 bytes
    sb->s_blocks_per_group = BLOCKS_COUNT; // All in one group
    sb->s_inodes_per_group = 1024;
    sb->s_first_data_block = 1;
    sb->s_rev_level = 1;
    sb->s_free_blocks_count = BLOCKS_COUNT - 200; // Arbitrary used count

    // 2. Block Group Descriptor at Block 2 (offset 2048)
    ext2_group_desc_t *bgd_disk = (ext2_group_desc_t *)(mock_disk + 2048);
    bgd_disk->bg_block_bitmap = 3;
    bgd_disk->bg_inode_bitmap = 4;
    bgd_disk->bg_inode_table = 5;
    bgd_disk->bg_free_blocks_count = BLOCKS_COUNT - 200;

    // 3. Block Bitmap at Block 3 (offset 3072)
    // Mark first ~200 blocks as used (metadata + some data)
    uint8_t *bitmap = mock_disk + 3072;
    memset(bitmap, 0xFF, 25); // 25 bytes = 200 bits

    // --- Setup In-Memory FS Structure ---

    ext2_fs_t fs;
    memset(&fs, 0, sizeof(fs));

    // Create a mock device node
    fs_node_t dev_node;
    memset(&dev_node, 0, sizeof(dev_node));
    dev_node.read = mock_read;
    dev_node.write = mock_write;

    fs.device = &dev_node;
    fs.sb = *sb;
    fs.block_size = 1024;
    fs.group_count = 1;
    fs.blocks_per_group = BLOCKS_COUNT;

    // Create a local BGD table for the FS struct
    ext2_group_desc_t bgd_table[1];
    bgd_table[0] = *bgd_disk;
    fs.bgd = bgd_table;

    // --- Benchmark Loop ---

    uint64_t start_tsc, end_tsc;
    uint32_t allocated_count = 0;
    uint32_t blocks_to_alloc = 2000;

    // Read TSC
    __asm__ volatile("rdtsc" : "=A"(start_tsc));

    for (uint32_t i = 0; i < blocks_to_alloc; i++) {
        uint32_t block = ext2_alloc_block(&fs);
        if (block != 0) {
            allocated_count++;
        } else {
            kprintf("Failed to allocate block at iteration %d\n", i);
            break;
        }
    }

    __asm__ volatile("rdtsc" : "=A"(end_tsc));

    if (allocated_count > 0) {
        uint64_t total_cycles = end_tsc - start_tsc;
        uint32_t cycles_low = (uint32_t)total_cycles;

        kprintf("EXT2 Alloc Perf: %u blocks in %u cycles (approx)\n", allocated_count, cycles_low);
        kprintf("Average cycles per block: %u\n", (uint32_t)(total_cycles / allocated_count));
    } else {
        kprint("EXT2 Alloc Perf: No blocks allocated.\n");
    }
}
