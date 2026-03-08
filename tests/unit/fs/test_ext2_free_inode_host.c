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
    return 0;
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

void run_ext2_free_inode_test(void) {
    printf("TEST: Ext2 Free Inode...\n");

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
    sb->s_free_inodes_count = 1024; // Initial count

    ext2_group_desc_t *bgd_disk = (ext2_group_desc_t *)(mock_disk + 2048);
    bgd_disk->bg_block_bitmap = 3;
    bgd_disk->bg_inode_bitmap = 4;
    bgd_disk->bg_inode_table = 5;
    bgd_disk->bg_free_blocks_count = BLOCKS_COUNT - 200;
    bgd_disk->bg_free_inodes_count = 1024; // Initial count
    bgd_disk->bg_used_dirs_count = 0;

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
    fs.inodes_per_group = 1024;
    fs.inode_size = 128;

    ext2_group_desc_t bgd_table[1];
    bgd_table[0] = *bgd_disk;
    fs.bgd = bgd_table;

    // Initialize allocator cache
    ext2_block_cache = uma_zcreate("ext2-block", 4096, NULL, NULL, NULL, NULL, 0, 0);

    // Initialize active inode bitmap cache
    fs.active_inode_bg_group = (uint32_t)-1;
    fs.active_inode_bg_bitmap = uma_zalloc(ext2_block_cache, M_WAITOK);


    uint32_t initial_free = fs.bgd[0].bg_free_inodes_count;

    // Allocate an inode (e.g. for a directory)
    uint32_t allocated_inode = ext2_alloc_inode(&fs, 1);

    if (allocated_inode == 0) {
        printf("FAILED: Could not allocate inode\n");
        exit(1);
    }

    printf("Allocated inode: %u\n", allocated_inode);

    if (fs.bgd[0].bg_free_inodes_count != initial_free - 1) {
        printf("FAILED: bg_free_inodes_count not decremented\n");
        exit(1);
    }

    if (fs.bgd[0].bg_used_dirs_count != 1) {
        printf("FAILED: bg_used_dirs_count not incremented\n");
        exit(1);
    }

    if (fs.sb.s_free_inodes_count != initial_free - 1) {
        printf("FAILED: s_free_inodes_count not decremented\n");
        exit(1);
    }

    // Now free the inode we just allocated
    ext2_free_inode(&fs, allocated_inode, 1);

    if (fs.bgd[0].bg_free_inodes_count != initial_free) {
        printf("FAILED: bg_free_inodes_count not restored (%u vs %u)\n", fs.bgd[0].bg_free_inodes_count, initial_free);
        exit(1);
    }

    if (fs.bgd[0].bg_used_dirs_count != 0) {
        printf("FAILED: bg_used_dirs_count not restored\n");
        exit(1);
    }

    if (fs.sb.s_free_inodes_count != initial_free) {
        printf("FAILED: s_free_inodes_count not restored\n");
        exit(1);
    }

    // Attempt to allocate again and see if we get the same inode
    uint32_t reallocated_inode = ext2_alloc_inode(&fs, 0);

    if (reallocated_inode != allocated_inode) {
        printf("FAILED: Did not re-allocate the same inode (%u vs %u)\n", reallocated_inode, allocated_inode);
        exit(1);
    }

    printf("SUCCESS: Free inode test passed\n");
}

int main() {
    run_ext2_free_inode_test();
    return 0;
}
