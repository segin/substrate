#include <kern/console.h>
#include <fs/ext2/ext2.h>
#include <string.h>
#include "tests.h"

// Forward declaration if needed
// extern uint32_t ext2_alloc_block(ext2_fs_t *fs);

#include <vm/vm_kmem.h>

// Mock disk parameters
#define BLOCK_SIZE 1024
#define BLOCKS_COUNT 256
#define MOCK_DISK_SIZE (BLOCKS_COUNT * BLOCK_SIZE)
static uint8_t *mock_disk = NULL;

static size_t mock_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    if (!mock_disk) return 0;
    if (offset + size > MOCK_DISK_SIZE) return 0;
    memcpy(buffer, mock_disk + offset, size);
    return size;
}

static size_t mock_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node;
    if (!mock_disk) return 0;
    if (offset + size > MOCK_DISK_SIZE) return 0;
    memcpy(mock_disk + offset, buffer, size);
    return size;
}

void run_ext2_read_perf_test(void) {
    kprint("TEST: Benchmarking Ext2 File Read...\n");

    // Allocate mock disk
    mock_disk = kmalloc(MOCK_DISK_SIZE);
    if (!mock_disk) {
        kprint("TEST FAILED: Could not allocate mock disk\n");
        return;
    }

    // Initialize mock disk with zeros
    memset(mock_disk, 0, MOCK_DISK_SIZE);

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
    sb->s_inode_size = 128; // Standard inode size

    // 2. Block Group Descriptor at Block 2 (offset 2048)
    ext2_group_desc_t *bgd_disk = (ext2_group_desc_t *)(mock_disk + 2048);
    bgd_disk->bg_block_bitmap = 3;
    bgd_disk->bg_inode_bitmap = 4;
    bgd_disk->bg_inode_table = 5; // Inode table at block 5
    bgd_disk->bg_free_blocks_count = BLOCKS_COUNT - 200;

    // 3. Create a file inode (Inode 10)
    // Inode table starts at block 5 (offset 5 * 1024 = 5120)
    // Inode size is 128.
    // Inode 10 is at index 9 (1-based).
    // Offset = 5120 + 9 * 128 = 5120 + 1152 = 6272
    ext2_inode_t *inode_disk = (ext2_inode_t *)(mock_disk + 5120 + (9 * 128));
    memset(inode_disk, 0, sizeof(ext2_inode_t));
    inode_disk->i_mode = EXT2_S_IFREG | 0644;
    inode_disk->i_size = 4096; // 4KB file
    inode_disk->i_links_count = 1;
    inode_disk->i_blocks = 8; // 4KB / 512 = 8 sectors
    inode_disk->i_block[0] = 100; // Data at block 100

    // 4. Populate Data Block 100 (offset 100 * 1024 = 102400)
    uint8_t *data_block = mock_disk + (100 * 1024);
    memset(data_block, 0xAA, 1024); // Fill with pattern

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
    fs.inodes_per_group = 1024;
    fs.inode_size = 128;

    // Create a local BGD table for the FS struct
    ext2_group_desc_t bgd_table[1];
    bgd_table[0] = *bgd_disk;
    fs.bgd = bgd_table;

    // --- Create File Node ---
    // We use ext2_alloc_node to get a properly initialized node (cached)
    // But we need to be careful not to rely on global state too much if tests run in parallel (they don't)

    // We need to read the inode into memory first?
    // ext2_alloc_node takes an ext2_inode_t pointer.
    ext2_inode_t inode_mem;
    memcpy(&inode_mem, inode_disk, sizeof(ext2_inode_t));

    fs_node_t *file_node = ext2_alloc_node(&fs, 10, &inode_mem);
    if (!file_node) {
        kprint("TEST FAILED: ext2_alloc_node returned NULL\n");
        return;
    }

    // --- Benchmark Loop ---

    uint64_t start_tsc, end_tsc;
    uint32_t iterations = 1000;
    uint8_t read_buf[1024];
    size_t total_bytes_read = 0;

    kprintf("Running %u reads of 1024 bytes...\n", iterations);

    // Read TSC
    __asm__ volatile("rdtsc" : "=A"(start_tsc));

    for (uint32_t i = 0; i < iterations; i++) {
        // Read from offset 0
        size_t bytes = ext2_file_read(file_node, 0, 1024, read_buf);
        if (bytes != 1024) {
             kprintf("Read failed at iteration %u, got %u bytes\n", i, bytes);
             break;
        }
        total_bytes_read += bytes;
    }

    __asm__ volatile("rdtsc" : "=A"(end_tsc));

    if (total_bytes_read > 0) {
        uint64_t total_cycles = end_tsc - start_tsc;
        uint32_t cycles_low = (uint32_t)total_cycles;

        kprintf("EXT2 Read Perf: %u iterations in %u cycles (approx)\n", iterations, cycles_low);
        kprintf("Average cycles per read: %u\n", (uint32_t)(total_cycles / iterations));
    } else {
        kprint("EXT2 Read Perf: No bytes read.\n");
    }
}
