#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

// Define HOST_TEST to enable any conditional logic in kernel headers
#ifndef HOST_TEST
#define HOST_TEST
#endif

// Needed types
#include <sys/types.h>
#include <vfs/vfs.h>
#include <sys/lock.h>

// Mock Kernel Functions
void kprint(const char *msg) {
    printf("[KERNEL] %s", msg);
    fflush(stdout);
}

// Mock VFS functions
void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

// Mock get_time
int64_t get_time(void) {
    return 0;
}

// Mock kmalloc/kfree
void *kmalloc(size_t size) {
    return calloc(1, size); // ext2 code often assumes zeroed memory for blocks? No, but safer.
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

// Mock Mutex
void mutex_init(mutex_t *m, const char *name) {
    (void)m; (void)name;
}
void mutex_lock(mutex_t *m) {
    (void)m;
}
void mutex_unlock(mutex_t *m) {
    (void)m;
}

// Rename colliding kernel function
#define vasprintf kernel_vasprintf

// Include the source under test
// This is relative to tests/unit/fs/
#include "../../../sys/fs/ext2/ext2.c"

// Mock UMA functions
// Implement them after including ext2.c (which includes uma.h), so we have the types.

uma_zone_t *uma_zcreate(const char *name, size_t size, uma_ctor ctor, uma_dtor dtor, uma_init init, uma_fini fini, int align, uint32_t flags) {
    (void)name; (void)ctor; (void)dtor; (void)init; (void)fini; (void)align; (void)flags;
    // We can't safely cast size_t* to uma_zone_t* if they have different alignment/size requirements strictly speaking,
    // but here we just need to store the size.
    size_t *s = malloc(sizeof(size_t));
    *s = size;
    return (uma_zone_t *)s;
}

void *uma_zalloc(uma_zone_t *zone, int flags) {
    (void)flags;
    if (!zone) return NULL;
    size_t size = *(size_t *)zone;
    return calloc(1, size);
}

void uma_zfree(uma_zone_t *zone, void *item) {
    (void)zone;
    free(item);
}


// ------------------------------------------------------------------
// Test Logic
// ------------------------------------------------------------------

// Using a larger disk for many inodes
#define BLOCKS_COUNT 32768
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

void setup_fs(ext2_fs_t *fs, fs_node_t *dev_node) {
    // Initialize mock disk with zeros
    memset(mock_disk, 0, sizeof(mock_disk));

    // --- Setup Filesystem Structures on "Disk" ---
    ext2_superblock_t *sb = (ext2_superblock_t *)(mock_disk + 1024);
    sb->s_magic = EXT2_SUPER_MAGIC;
    sb->s_blocks_count = BLOCKS_COUNT;
    sb->s_log_block_size = 0; // 1024 bytes
    sb->s_blocks_per_group = BLOCKS_COUNT; // All in one group
    sb->s_inodes_per_group = 16384; // Enough inodes for our test
    sb->s_first_data_block = 1;
    sb->s_rev_level = 1;
    sb->s_inode_size = 128;
    sb->s_free_blocks_count = BLOCKS_COUNT - 200;
    sb->s_free_inodes_count = 16384;

    // Block Group Descriptor (at block 2)
    ext2_group_desc_t *bgd_disk = (ext2_group_desc_t *)(mock_disk + 2048);
    bgd_disk->bg_block_bitmap = 3;
    bgd_disk->bg_inode_bitmap = 4;
    bgd_disk->bg_inode_table = 5; // Starts at block 5
    // Inode table size: 16384 * 128 / 1024 = 2048 blocks. Ends at block 2053.
    bgd_disk->bg_free_blocks_count = BLOCKS_COUNT - 3000;
    bgd_disk->bg_free_inodes_count = 16384;
    bgd_disk->bg_used_dirs_count = 0;

    // Mark bitmaps as free (0) mostly. block 3 and 4 are bitmaps.
    // They are already 0 from memset.

    // --- Setup In-Memory FS Structure ---
    memset(fs, 0, sizeof(ext2_fs_t));

    memset(dev_node, 0, sizeof(fs_node_t));
    dev_node->read = mock_read;
    dev_node->write = mock_write;

    fs->device = dev_node;
    fs->sb = *sb;
    fs->block_size = 1024;
    fs->group_count = 1;
    fs->blocks_per_group = BLOCKS_COUNT;
    fs->inodes_per_group = 16384;
    fs->inode_size = 128;

    // Allocate memory for BGD table in FS structure
    // Since we mock kmalloc, we can just use a static buffer or malloc
    // ext2_fs_t in ext2.c uses a pointer. We need to point it to something valid.
    // In ext2.c: static ext2_group_desc_t ext2_bgd_table[64];
    // But here fs is local. We should allocate bgd table.
    fs->bgd = (ext2_group_desc_t *)calloc(1, sizeof(ext2_group_desc_t));
    fs->bgd[0] = *bgd_disk;
}

void run_ext2_remove_perf_test(void) {
    printf("TEST: Benchmarking Ext2 Remove Entry (Host)...\n");

    ext2_fs_t fs;
    fs_node_t dev_node;
    setup_fs(&fs, &dev_node);

    // Create a directory inode
    // Inode 1 is bad, 2 is root. Let's use 2 as our test directory.
    // We need to write inode 2 to disk first.
    ext2_inode_t root_inode;
    memset(&root_inode, 0, sizeof(root_inode));
    root_inode.i_mode = EXT2_S_IFDIR | 0755;
    root_inode.i_size = 1024; // Initially one block
    root_inode.i_links_count = 2;
    root_inode.i_blocks = 2; // 1 data block + metadata overhead

    // Allocate block for directory data
    root_inode.i_block[0] = 100; // Arbitrary free block

    // Initialize the directory block to be a valid empty block
    // It must have at least one entry covering the whole block
    uint8_t *dir_block = mock_disk + 100 * 1024;
    ext2_dirent_t *de = (ext2_dirent_t *)dir_block;
    de->inode = 0; // Unused
    de->rec_len = 1024; // Covers whole block
    de->name_len = 0;

    // Write root inode to table
    // ext2_write_inode(&fs, 2, &root_inode); // Fails because it tries to read block first?
    // We can write directly to mock disk or rely on ext2_write_inode if it works.
    // ext2_write_inode allocates buffer and reads block. It should work.
    if (ext2_write_inode(&fs, 2, &root_inode) != 0) {
        printf("Failed to write root inode\n");
        return;
    }

    // Create fs_node_t for the directory
    fs_node_t *dir = ext2_alloc_node(&fs, 2, &root_inode);
    if (!dir) {
        printf("Failed to alloc dir node\n");
        return;
    }

    // Add many entries
    int num_entries = 500;
    printf("Adding %d entries...\n", num_entries);

    char name[32];
    for (int i = 0; i < num_entries; i++) {
        sprintf(name, "file_%d", i);
        // We use dummy inode numbers (e.g., 10 + i)
        if (ext2_add_entry(dir, name, 10 + i) != 0) {
            printf("Failed to add entry %d\n", i);
            break;
        }
    }
    printf("Entries added.\n");

    // Benchmark removal of the LAST added entry (worst case search)
    sprintf(name, "file_%d", num_entries - 1);
    printf("Removing entry: %s\n", name);

    uint64_t start_tsc = rdtsc_host();

    int result = ext2_remove_entry(dir, name);

    uint64_t end_tsc = rdtsc_host();

    if (result != 0) {
        printf("Failed to remove entry!\n");
    } else {
        printf("Removal successful.\n");
        printf("Cycles taken: %lu\n", end_tsc - start_tsc);
    }

    // Cleanup
    free(fs.bgd);
    // free(dir); // allocated in cache, tricky to free in this test setup
}

int main() {
    setbuf(stdout, NULL);
    ext2_init(); // Initialize block cache
    for (int i = 0; i < 10; i++) {
        run_ext2_remove_perf_test();
    }
    return 0;
}
