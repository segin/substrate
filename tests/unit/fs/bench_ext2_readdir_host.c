#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>

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

int kprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vprintf(fmt, args);
    va_end(args);
    return ret;
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
bool mutex_trylock(mutex_t *m) { (void)m; return true; }

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
    uma_zone_t *zone = (uma_zone_t *)calloc(1, sizeof(uma_zone_t));
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

void run_ext2_readdir_bench(void) {
    printf("TEST: Benchmarking Ext2 Readdir Sequential Access (Host)...\n");

    // Initialize mock disk with zeros
    memset(mock_disk, 0, sizeof(mock_disk));

    // --- Setup Filesystem Structures on "Disk" ---
    ext2_superblock_t *sb = (ext2_superblock_t *)(mock_disk + 1024);
    sb->s_magic = EXT2_SUPER_MAGIC;
    sb->s_blocks_count = BLOCKS_COUNT;
    sb->s_log_block_size = 0; // 1024 bytes
    sb->s_blocks_per_group = BLOCKS_COUNT; // All in one group
    sb->s_inodes_per_group = 2048; // Enough inodes
    sb->s_first_data_block = 1;
    sb->s_rev_level = 1;
    sb->s_free_blocks_count = BLOCKS_COUNT - 200;
    sb->s_free_inodes_count = 2000;

    ext2_group_desc_t *bgd_disk = (ext2_group_desc_t *)(mock_disk + 2048);
    bgd_disk->bg_block_bitmap = 3;
    bgd_disk->bg_inode_bitmap = 4;
    bgd_disk->bg_inode_table = 5;
    bgd_disk->bg_free_blocks_count = BLOCKS_COUNT - 200;
    bgd_disk->bg_free_inodes_count = 2000;

    uint8_t *bitmap = mock_disk + 3072; // Block bitmap
    memset(bitmap, 0xFF, 25); // Mark metadata blocks used

    uint8_t *ibitmap = mock_disk + 4096; // Inode bitmap (block 4)
    memset(ibitmap, 0, 1024); // All free initially

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
    fs.inodes_per_group = 2048;
    fs.inode_size = 128; // Default

    ext2_group_desc_t bgd_table[1];
    bgd_table[0] = *bgd_disk;
    fs.bgd = bgd_table;

    // Initialize allocator cache
    ext2_block_cache = uma_zcreate("ext2-block", 4096, NULL, NULL, NULL, NULL, 0, 0);

    // Create a directory inode
    uint32_t dir_inode = ext2_alloc_inode(&fs, 1);
    ext2_inode_t inode_struct;
    memset(&inode_struct, 0, sizeof(inode_struct));
    inode_struct.i_mode = EXT2_S_IFDIR | 0755;
    inode_struct.i_links_count = 2;
    ext2_write_inode(&fs, dir_inode, &inode_struct);

    fs_node_t *dir = ext2_alloc_node(&fs, dir_inode, &inode_struct);

    // Populate directory with 2000 entries
    int entry_count = 2000;
    char name[32];
    for (int i = 0; i < entry_count; i++) {
        sprintf(name, "file_%d", i);
        // We use dummy inode 100+i for entries
        ext2_add_entry(dir, name, 100 + i);
    }

    printf("Created %d entries in directory.\n", entry_count);

    // --- Benchmark Loop ---
    uint64_t start_tsc, end_tsc;

    start_tsc = rdtsc_host();

    // Iterate sequentially mimicking find_name_by_inode behavior
    // It calls readdir(dir, 0), readdir(dir, 1), ...
    for (int i = 0; i < entry_count; i++) {
        struct dirent *d = ext2_readdir(dir, i);
        if (!d) {
            printf("Error: failed to read entry %d\n", i);
            break;
        }
        // Verify (optional)
        // printf("Entry %d: %s\n", i, d->d_name);
    }

    end_tsc = rdtsc_host();

    uint64_t total_cycles = end_tsc - start_tsc;
    printf("EXT2 Readdir Perf (N=%d): %lu cycles\n", entry_count, total_cycles);
    printf("Average cycles per entry: %lu\n", total_cycles / entry_count);
}

int main() {
    run_ext2_readdir_bench();
    return 0;
}
