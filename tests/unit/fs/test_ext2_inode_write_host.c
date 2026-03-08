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

void *kmalloc(size_t size) {
    return calloc(1, size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

// Mock Mutex
#include <sys/lock.h>
void mutex_init(mutex_t *m, const char *name) { (void)m; (void)name; }
void mutex_lock(mutex_t *m) { (void)m; }
void mutex_unlock(mutex_t *m) { (void)m; }

// Mock VFS functions
void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

// Mock get_time
int64_t get_time(void) {
    return 123456789;
}

// UMA Mocks for Ext2
#include <vm/uma.h>

uma_zone_t *uma_zcreate(const char *name, size_t size, uma_ctor ctor, uma_dtor dtor, uma_init init, uma_fini fini, int align, uint32_t flags) {
    (void)ctor; (void)dtor; (void)init; (void)fini; (void)align; (void)flags;
    uma_zone_t *zone = (uma_zone_t *)calloc(1, sizeof(uma_zone_t)); // use calloc to avoid uninit
    if (zone) {
        zone->uz_name = name;
        zone->uz_size = size;
        zone->uz_flags = flags;
    }
    return zone;
}

void *uma_zalloc(uma_zone_t *zone, int flags) {
    (void)flags;
    if (!zone) return NULL;
    return calloc(1, zone->uz_size);
}

void uma_zfree(uma_zone_t *zone, void *item) {
    (void)zone;
    free(item);
}

void uma_zone_set_max(uma_zone_t *zone, int max) { (void)zone; (void)max; }


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

void run_test_ext2_inode_write() {
    printf("TEST: ext2_inode_write\n");

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

    ext2_block_cache = uma_zcreate("ext2-block", 4096, NULL, NULL, NULL, NULL, 0, 0);

    fs.active_bg_group = (uint32_t)-1;
    fs.active_bg_bitmap = uma_zalloc(ext2_block_cache, M_WAITOK);

    // Setup the node and inode
    ext2_node_t node;
    memset(&node, 0, sizeof(node));
    node.fs = &fs;
    mutex_init(&node.lock, "test_lock");

    // Create an empty inode
    ext2_inode_t *inode = &node.inode;
    inode->i_size = 0;

    // Test 1: Write a small amount of data to direct block
    const char *test_data = "Hello, world!";
    uint32_t written = ext2_inode_write(&node, 0, strlen(test_data), test_data);
    if (written != strlen(test_data)) {
        printf("FAILED Test 1: wrote %u bytes, expected %zu\n", written, strlen(test_data));
        exit(1);
    }

    // Verify block allocation
    if (inode->i_block[0] == 0) {
        printf("FAILED Test 1: block 0 not allocated\n");
        exit(1);
    }

    // Verify data written
    char read_buf[1024] = {0};
    ext2_read_block(&fs, inode->i_block[0], read_buf);
    if (memcmp(read_buf, test_data, strlen(test_data)) != 0) {
        printf("FAILED Test 1: data mismatch. Read: %s\n", read_buf);
        exit(1);
    }
    printf("Test 1 passed: Direct block write\n");

    // Test 2: Write spanning blocks
    char span_data[2048];
    for(int i=0; i<2048; i++) span_data[i] = (char)(i % 256);

    written = ext2_inode_write(&node, 1000, 2048, span_data); // Spans blocks 0, 1, 2
    if (written != 2048) {
        printf("FAILED Test 2: wrote %u bytes, expected 2048\n", written);
        exit(1);
    }
    if (inode->i_block[1] == 0 || inode->i_block[2] == 0) {
        printf("FAILED Test 2: blocks 1 or 2 not allocated\n");
        exit(1);
    }

    // verify some data from the middle of the span
    ext2_read_block(&fs, inode->i_block[1], read_buf);
    if (read_buf[0] != span_data[24]) { // 1000+24 = 1024 (start of block 1)
        printf("FAILED Test 2: data mismatch in block 1. Expected %d, got %d\n", span_data[24], read_buf[0]);
        exit(1);
    }
    printf("Test 2 passed: Spanning blocks write\n");

    // Test 3: Indirect block write
    char indirect_data[100];
    memset(indirect_data, 0xAB, 100);
    written = ext2_inode_write(&node, 12 * 1024 + 10, 100, indirect_data); // Block 12 is indirect
    if (written != 100) {
        printf("FAILED Test 3: wrote %u bytes, expected 100\n", written);
        exit(1);
    }

    if (inode->i_block[12] == 0) {
        printf("FAILED Test 3: indirect block not allocated\n");
        exit(1);
    }

    // Read the indirect block to get the data block
    uint32_t indirect_ptrs[256];
    ext2_read_block(&fs, inode->i_block[12], indirect_ptrs);
    if (indirect_ptrs[0] == 0) {
        printf("FAILED Test 3: data block in indirect block not allocated\n");
        exit(1);
    }

    ext2_read_block(&fs, indirect_ptrs[0], read_buf);
    if (memcmp(read_buf + 10, indirect_data, 100) != 0) {
        printf("FAILED Test 3: data mismatch in indirect data block\n");
        exit(1);
    }
    printf("Test 3 passed: Indirect block write\n");

    printf("ALL TESTS PASSED\n");
}

int main() {
    run_test_ext2_inode_write();
    return 0;
}
