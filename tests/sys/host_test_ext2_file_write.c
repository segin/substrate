#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

// Mock Types
typedef long off_t;

// Mock kprint
void kprint(const char *str) {
    (void)str;
}

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
void setup_fs(ext2_fs_t *fs, fs_node_t *dev_node, ext2_inode_t *inode, ext2_node_t *node_ctx, fs_node_t *file_node, ext2_group_desc_t *bgd) {
    memset(fs, 0, sizeof(ext2_fs_t));
    memset(dev_node, 0, sizeof(fs_node_t));

    dev_node->read = mock_read;
    dev_node->write = mock_write;

    fs->device = dev_node;
    fs->block_size = 1024;
    fs->inode_size = 128;
    fs->inodes_per_group = 100;
    fs->blocks_per_group = 10000;
    fs->group_count = 1;
    fs->bgd = bgd;

    memset(bgd, 0, sizeof(ext2_group_desc_t));
    bgd->bg_free_blocks_count = 100;
    bgd->bg_block_bitmap = 1; // mock disk block 1 is bitmap

    fs->active_bg_bitmap = kmalloc(fs->block_size);
    fs->active_bg_group = 0; // Pre-cache the bitmap for group 0
    // make sure bitmap block 1 has free space
    memset(fs->active_bg_bitmap, 0, fs->block_size);

    // Initialize mocks
    ext2_init(); // creates uma zones

    // Setup File Inode
    memset(inode, 0, sizeof(ext2_inode_t));
    inode->i_mode = EXT2_S_IFREG | 0644;
    inode->i_size = 0; // 0 bytes initially
    inode->i_block[0] = 0; // Unallocated

    // Setup Node Context
    memset(node_ctx, 0, sizeof(ext2_node_t));
    node_ctx->fs = fs;
    node_ctx->inode_num = 10; // File
    memcpy(&node_ctx->inode, inode, sizeof(ext2_inode_t));
    mutex_init(&node_ctx->lock, "file_lock");

    // Setup FS Node
    memset(file_node, 0, sizeof(fs_node_t));
    file_node->impl = (uintptr_t)node_ctx;
    file_node->length = 0;
}

int main() {
    mock_disk = calloc(1, mock_disk_size);
    if (!mock_disk) return 1;

    ext2_fs_t fs;
    fs_node_t dev_node;
    ext2_inode_t inode;
    ext2_node_t node_ctx;
    fs_node_t file_node;
    ext2_group_desc_t bgd;

    setup_fs(&fs, &dev_node, &inode, &node_ctx, &file_node, &bgd);

    printf("Testing ext2_file_write...\n");

    const uint8_t *test_data = (const uint8_t *)"Hello, Ext2 Write Test!";
    size_t len = strlen((const char *)test_data);

    // We just set a block to avoid needing to allocate during write test
    node_ctx.inode.i_block[0] = 100;

    // Let's print out what size we are writing
    printf("Writing %zu bytes at offset 0\n", len);

    // Test writing to the file
    size_t written = ext2_file_write(&file_node, 0, len, test_data);

    if (written != len) {
        printf("FAILED: ext2_file_write returned %zu, expected %zu\n", written, len);
        return 1;
    }

    if (file_node.length != len) {
        printf("FAILED: file_node.length is %zu, expected %zu\n", file_node.length, len);
        return 1;
    }

    if (node_ctx.inode.i_size != len) {
        printf("FAILED: node_ctx.inode.i_size is %u, expected %zu\n", node_ctx.inode.i_size, len);
        return 1;
    }

    // Check if data was written to the mock disk at the correct block
    uint32_t block = node_ctx.inode.i_block[0];
    if (block == 0) {
        printf("FAILED: Block was not allocated\n");
        return 1;
    }

    if (memcmp(mock_disk + (block * 1024), test_data, len) != 0) {
        printf("FAILED: data on disk does not match written data\n");
        return 1;
    }

    printf("SUCCESS: ext2_file_write test passed.\n");

    free(mock_disk);
    return 0;
}
