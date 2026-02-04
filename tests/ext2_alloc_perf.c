#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>

// Resolve conflicts
#define vasprintf kernel_vasprintf

// Include the source file
#include "../sys/fs/ext2/ext2.c"

// Mock implementations
void kprint(const char *str) {
    printf("%s", str);
}

void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

int64_t get_time(void) {
    return time(NULL);
}

// Device Mock
static uint8_t *virtual_disk = NULL;
static size_t virtual_disk_size = 0;
static uint32_t block_read_count = 0;

static size_t mock_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    if (offset + size > virtual_disk_size) return 0;
    memcpy(buffer, virtual_disk + offset, size);

    // Count block reads (approximate)
    if (size == 1024 || size == 4096) {
        block_read_count++;
    }
    return size;
}

static size_t mock_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node;
    if (offset + size > virtual_disk_size) {
        return 0;
    }
    memcpy(virtual_disk + offset, buffer, size);
    return size;
}

// Helpers to setup the benchmark
#define BLOCK_SIZE 1024
#define GROUPS 64
#define BLOCKS_PER_GROUP 8192
#define INODES_PER_GROUP 2048

void setup_filesystem() {
    virtual_disk_size = (size_t)GROUPS * BLOCKS_PER_GROUP * BLOCK_SIZE;
    virtual_disk = mmap(NULL, virtual_disk_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (virtual_disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    // Format the disk with superblock and BGD
    ext2_superblock_t *sb = (ext2_superblock_t *)(virtual_disk + 1024);
    sb->s_magic = EXT2_SUPER_MAGIC;
    sb->s_log_block_size = 0; // 1024
    sb->s_blocks_per_group = BLOCKS_PER_GROUP;
    sb->s_inodes_per_group = INODES_PER_GROUP;
    sb->s_blocks_count = GROUPS * BLOCKS_PER_GROUP;
    sb->s_first_data_block = 1;
    sb->s_rev_level = 1;
    sb->s_inode_size = 128;

    // Setup Group Descriptors
    // Located at block 2 (since block size is 1024)
    ext2_group_desc_t *bgd = (ext2_group_desc_t *)(virtual_disk + 2 * BLOCK_SIZE);

    for (int i = 0; i < GROUPS; i++) {
        uint32_t group_start = 1 + i * BLOCKS_PER_GROUP;

        bgd[i].bg_block_bitmap = group_start + 1;
        bgd[i].bg_inode_bitmap = group_start + 2;
        bgd[i].bg_inode_table = group_start + 3;

        bgd[i].bg_free_blocks_count = BLOCKS_PER_GROUP - 100;
        bgd[i].bg_free_inodes_count = INODES_PER_GROUP;

        uint32_t bitmap_offset = bgd[i].bg_block_bitmap * BLOCK_SIZE;
        memset(virtual_disk + bitmap_offset, 0, BLOCK_SIZE);
    }
}

int main() {
    setup_filesystem();

    fs_node_t device_node;
    memset(&device_node, 0, sizeof(device_node));
    device_node.read = mock_read;
    device_node.write = mock_write;

    // Mount
    // We pass &device_node as 'data' which is cast to fs_node_t* in ext2_mount
    fs_node_t *root = ext2_filesystem.mount(NULL, 0, &device_node);
    if (!root) {
        printf("Failed to mount\n");
        return 1;
    }

    printf("Filesystem mounted.\n");

    // Benchmark allocation - stress test intra-group search
    // We will allocate many blocks which should fill the first group
    printf("Benchmarking allocation (filling a group)...\n");

    clock_t start = clock();
    block_read_count = 0;

    // Allocate 5000 blocks.
    // This fits in the first group (8192 blocks).
    // With linear search from 0, this should trigger O(N^2) bit checks.
    int alloc_count = 5000;
    for (int i = 0; i < alloc_count; i++) {
        uint32_t block = ext2_alloc_block(&ext2_fs);
        if (block == 0) {
            printf("Allocation failed at iteration %d\n", i);
            break;
        }
    }

    clock_t end = clock();
    double time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;

    printf("Time taken for %d allocations: %f seconds\n", alloc_count, time_taken);
    printf("Total block reads: %u\n", block_read_count);
    printf("Reads per allocation: %f\n", (double)block_read_count / alloc_count);

    return 0;
}
