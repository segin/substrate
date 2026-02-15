#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

// Mock Counters
static int strlen_cnt = 0;
static int strncmp_cnt = 0;
static int memcmp_cnt = 0;

// Wrappers
size_t my_strlen(const char *s) {
    strlen_cnt++;
    return strlen(s);
}

int my_strncmp(const char *s1, const char *s2, size_t n) {
    strncmp_cnt++;
    return strncmp(s1, s2, n);
}

int my_memcmp(const void *s1, const void *s2, size_t n) {
    memcmp_cnt++;
    return memcmp(s1, s2, n);
}

// Intercept standard functions
#define strlen my_strlen
#define strncmp my_strncmp
#define memcmp my_memcmp

// Mock Types
typedef long off_t;

// Mock Lock
#include <sys/lock.h>

void spinlock_init(spinlock_t *lock, const char *name) { (void)lock; (void)name; }
void spinlock_acquire(spinlock_t *lock) { (void)lock; }
void spinlock_release(spinlock_t *lock) { (void)lock; }
bool spinlock_is_held(spinlock_t *lock) { (void)lock; return false; }

void mutex_init(mutex_t *m, const char *name) {
    m->locked = 0;
    m->name = name;
}
void mutex_lock(mutex_t *m) { m->locked = 1; }
void mutex_unlock(mutex_t *m) { m->locked = 0; }
bool mutex_trylock(mutex_t *m) { m->locked = 1; return true; }
bool mutex_is_held(mutex_t *m) { return m->locked != 0; }

// Mock kprint
void kprint(const char *str) {
    (void)str;
    // printf("[KERNEL] %s", str);
}

// Mock kmalloc
#include <vm/vm_kmem.h>
void *kmalloc(size_t size) {
    return calloc(1, size);
}
void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

// Mock VFS
#include <vfs/vfs.h>
void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

// Mock Device I/O
static uint8_t *mock_disk = NULL;
static size_t mock_disk_size = 1024 * 1024; // 1MB

size_t mock_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    if (offset + size > mock_disk_size) return 0;
    memcpy(buffer, mock_disk + offset, size);
    return size;
}

size_t mock_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    if (offset + size > mock_disk_size) return 0;
    memcpy(mock_disk + offset, buffer, size);
    return size;
}

// Mock Time
int64_t get_time(void) { return 0; }

// Mock UMA
#include <vm/uma.h>

void uma_startup(void) {}
void uma_enable_dynamic_alloc(void) {}

uma_zone_t *uma_zcreate(const char *name, size_t size, uma_ctor ctor, uma_dtor dtor,
                        uma_init init, uma_fini fini, int align, uint32_t flags) {
    uma_zone_t *z = (uma_zone_t*)malloc(sizeof(uma_zone_t));
    if (!z) return NULL;
    z->uz_name = name;
    z->uz_size = size;
    return z;
}

void uma_zdestroy(uma_zone_t *zone) { free(zone); }

void *uma_zalloc(uma_zone_t *zone, int flags) {
    return calloc(1, zone->uz_size > 0 ? zone->uz_size : 4096);
}

void uma_zfree(uma_zone_t *zone, void *item) {
    free(item);
}

// Include source
#define vasprintf kernel_vasprintf
#include "../../sys/fs/ext2/ext2.c"

// Test Setup
void setup_fs(ext2_fs_t *fs, fs_node_t *dev_node, ext2_inode_t *inode, ext2_node_t *node_ctx, fs_node_t *dir_node) {
    memset(fs, 0, sizeof(ext2_fs_t));
    memset(dev_node, 0, sizeof(fs_node_t));

    dev_node->read = mock_read;
    dev_node->write = mock_write;

    fs->device = dev_node;
    fs->block_size = 1024;
    fs->inodes_per_group = 100;
    fs->blocks_per_group = 10000;

    // Initialize mocks
    ext2_init(); // creates uma zones

    // Setup Directory Inode
    memset(inode, 0, sizeof(ext2_inode_t));
    inode->i_mode = EXT2_S_IFDIR | 0755;
    inode->i_size = 1024; // 1 block
    inode->i_block[0] = 100; // Physical block 100

    // Setup Node Context
    memset(node_ctx, 0, sizeof(ext2_node_t));
    node_ctx->fs = fs;
    node_ctx->inode_num = 2; // Root
    memcpy(&node_ctx->inode, inode, sizeof(ext2_inode_t));
    mutex_init(&node_ctx->lock, "dir_lock");

    // Setup FS Node
    memset(dir_node, 0, sizeof(fs_node_t));
    dir_node->impl = (uintptr_t)node_ctx;

    // Create Directory Entries in Block 100
    uint8_t *block = malloc(1024);
    memset(block, 0, 1024);

    // . (current)
    ext2_dirent_t *de = (ext2_dirent_t *)block;
    de->inode = 2;
    de->rec_len = 12;
    de->name_len = 1;
    de->file_type = EXT2_FT_DIR;
    memcpy(de->name, ".", 1);

    // .. (parent)
    de = (ext2_dirent_t *)(block + 12);
    de->inode = 2;
    de->rec_len = 12;
    de->name_len = 2;
    de->file_type = EXT2_FT_DIR;
    memcpy(de->name, "..", 2);

    // entry1
    de = (ext2_dirent_t *)(block + 24);
    de->inode = 10;
    de->rec_len = 16;
    de->name_len = 6;
    de->file_type = EXT2_FT_REG_FILE;
    memcpy(de->name, "entry1", 6);

    // target (to remove)
    de = (ext2_dirent_t *)(block + 40);
    de->inode = 11;
    de->rec_len = 16;
    de->name_len = 6;
    de->file_type = EXT2_FT_REG_FILE;
    memcpy(de->name, "target", 6);

    // entry3 (last) - takes rest of block
    de = (ext2_dirent_t *)(block + 56);
    de->inode = 12;
    de->rec_len = 1024 - 56;
    de->name_len = 6;
    de->file_type = EXT2_FT_REG_FILE;
    memcpy(de->name, "entry3", 6);

    // Write to disk
    mock_write(dev_node, 100 * 1024, 1024, block);
    free(block);
}

int main() {
    mock_disk = calloc(1, mock_disk_size);
    if (!mock_disk) return 1;

    ext2_fs_t fs;
    fs_node_t dev_node;
    ext2_inode_t inode;
    ext2_node_t node_ctx;
    fs_node_t dir_node;

    setup_fs(&fs, &dev_node, &inode, &node_ctx, &dir_node);

    printf("Testing ext2_remove_entry('target')...\n");

    // Reset counters
    strlen_cnt = 0;
    strncmp_cnt = 0;
    memcmp_cnt = 0;

    int ret = ext2_remove_entry(&dir_node, "target");

    if (ret != 0) {
        printf("FAILED: ext2_remove_entry returned %d\n", ret);
        return 1;
    }

    printf("strlen calls: %d\n", strlen_cnt);
    printf("strncmp calls: %d\n", strncmp_cnt);
    printf("memcmp calls: %d\n", memcmp_cnt);

    if (strlen_cnt != 1) {
        printf("FAIL: strlen called %d times (expected 1)\n", strlen_cnt);
        return 1;
    }

    if (strncmp_cnt != 0) {
        printf("FAIL: strncmp called %d times (expected 0)\n", strncmp_cnt);
        return 1;
    }

    if (memcmp_cnt != 2) {
        printf("FAIL: memcmp called %d times (expected 2)\n", memcmp_cnt);
        return 1;
    }

    printf("SUCCESS: Verification passed.\n");

    free(mock_disk);
    return 0;
}
