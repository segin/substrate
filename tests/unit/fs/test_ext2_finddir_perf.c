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
    printf("[KERNEL] %s", msg); // Enable kernel logs
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

uint64_t rdtsc_host() {
    unsigned int lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi) :: "memory");
    return ((uint64_t)hi << 32) | lo;
}

void run_ext2_finddir_perf_test(void) {
    printf("TEST: Benchmarking Ext2 Finddir (Host)...\n");

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
    sb->s_first_ino = 11;
    sb->s_inode_size = 128;

    ext2_group_desc_t *bgd_disk = (ext2_group_desc_t *)(mock_disk + 2048);
    bgd_disk->bg_block_bitmap = 3;
    bgd_disk->bg_inode_bitmap = 4;
    bgd_disk->bg_inode_table = 5;
    bgd_disk->bg_free_blocks_count = BLOCKS_COUNT - 200;
    bgd_disk->bg_free_inodes_count = 1000;
    bgd_disk->bg_used_dirs_count = 0;

    // Setup bitmaps
    // Block bitmap (block 3)
    uint8_t *block_bitmap = mock_disk + 3 * 1024;
    memset(block_bitmap, 0, 1024);
    // Mark first few blocks as used (superblock, bgd, bitmaps, inode table)
    // Blocks 0-200 used
    for(int i=0; i<25; i++) block_bitmap[i] = 0xFF; // 25 * 8 = 200 blocks

    // Inode bitmap (block 4)
    uint8_t *inode_bitmap = mock_disk + 4 * 1024;
    memset(inode_bitmap, 0, 1024);
    // Mark reserved inodes (1-10) as used
    inode_bitmap[0] = 0xFF;
    inode_bitmap[1] = 0x03; // bits 0-9 set

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

    // Initialize block cache for allocator
    ext2_block_cache = uma_zcreate("ext2-block", 4096, NULL, NULL, NULL, NULL, 0, 0);

    // --- Create Directory Inode ---
    uint32_t dir_inode_num = ext2_alloc_inode(&fs, 1);
    if (dir_inode_num == 0) {
        printf("ERROR: Failed to allocate directory inode\n");
        return;
    }

    ext2_inode_t dir_inode;
    ext2_read_inode(&fs, dir_inode_num, &dir_inode);
    dir_inode.i_mode = EXT2_S_IFDIR | 0755;
    dir_inode.i_size = 1024; // Initial size (empty block)
    // Allocate first block for directory
    uint32_t dir_block = ext2_alloc_block(&fs);
    dir_inode.i_block[0] = dir_block;
    // Init block with empty entry
    ext2_dirent_t *de = (ext2_dirent_t *)(mock_disk + dir_block * 1024);
    de->inode = 0;
    de->rec_len = 1024;
    ext2_write_inode(&fs, dir_inode_num, &dir_inode);

    fs_node_t *dir_node = ext2_alloc_node(&fs, dir_inode_num, &dir_inode);

    // --- Populate Directory ---
    int num_files = 500; // Use 500 to keep setup time reasonable
    printf("Populating directory with %d files...\n", num_files);

    char name_buf[32];
    for (int i = 0; i < num_files; i++) {
        sprintf(name_buf, "file%d", i);
        // We need a dummy inode for the entry
        if (ext2_add_entry(dir_node, name_buf, 100 + i) != 0) {
            printf("ERROR: Failed to add entry %s\n", name_buf);
            break;
        }
        if (i % 500 == 0) printf(".");
    }
    printf("\nDone populating.\n");

    // --- Benchmark Finddir ---
    printf("Directory Size: %llu bytes\n", dir_node->length);

    // Verify file0
    sprintf(name_buf, "file0");
    fs_node_t *f0 = ext2_finddir(dir_node, name_buf);
    if (f0) printf("FOUND: file0\n");
    else printf("ERROR: file0 not found\n");

    // Search for the last file (worst case)
    sprintf(name_buf, "file%d", num_files - 1);

    uint64_t start_tsc = rdtsc_host();
    fs_node_t *found = ext2_finddir(dir_node, name_buf);
    uint64_t end_tsc = rdtsc_host();

    if (found) {
        printf("FOUND: %s in %lu cycles\n", found->name, end_tsc - start_tsc);
        // kfree(found, sizeof(fs_node_t)); // Mock kfree ignores size but technically incorrect
    } else {
        printf("ERROR: file%d not found!\n", num_files - 1);
    }

    // Benchmark loop
    int iterations = 50; // Must be < 64 to avoid cache wrap-around clobbering dir_node
    uint64_t total_cycles = 0;

    for (int i = 0; i < iterations; i++) {
        start_tsc = rdtsc_host();
        fs_node_t *node = ext2_finddir(dir_node, name_buf);
        end_tsc = rdtsc_host();
        total_cycles += (end_tsc - start_tsc);
        if (node) {
             if (node->name[0] != 'f') printf("Bad Name\n"); // Force usage
        } else {
             printf("NULL in loop!\n");
             exit(1);
        }
    }

    printf("Average cycles per finddir (worst case): %lu\n", total_cycles / iterations);

    // Cleanup
    free(ext2_block_cache);
}

int main() {
    run_ext2_finddir_perf_test();
    return 0;
}
