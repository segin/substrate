#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

// ==========================================
// Mocks for Kernel Environment
// ==========================================

typedef long off_t;

void kprint(const char *str) {
    (void)str;
}

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

#include <vm/vm_kmem.h>
void *kmalloc(size_t size) {
    return calloc(1, size);
}
void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

#include <vfs/vfs.h>

void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

int64_t get_time(void) {
    return 0;
}

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

// Define kernel_vasprintf to avoid unresolved symbol
#define vasprintf kernel_vasprintf
int kernel_vasprintf(char **strp, const char *fmt, va_list ap) {
    (void)strp; (void)fmt; (void)ap;
    return 0;
}

// Mock Device Implementation
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

// Include ext2 definitions
#include <fs/ext2/ext2.h>

void ext2_init(void);
uint32_t ext2_get_block_num(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t block_idx,
                                   uint32_t *indirect_buf, uint32_t *dindirect_buf, uint32_t *tindirect_buf);

int main() {
    printf("Testing ext2_get_blocks_extent...\n");

    mock_disk = calloc(1, mock_disk_size);
    if (!mock_disk) return 1;

    ext2_fs_t fs;
    memset(&fs, 0, sizeof(fs));

    fs_node_t dev_node;
    memset(&dev_node, 0, sizeof(fs_node_t));
    dev_node.read = mock_read;
    dev_node.write = mock_write;

    fs.device = &dev_node;
    fs.block_size = 1024;
    fs.inodes_per_group = 100;
    fs.blocks_per_group = 10000;

    ext2_init();

    ext2_inode_t inode;
    memset(&inode, 0, sizeof(ext2_inode_t));

    // Set up some blocks
    // Direct blocks: 0-11
    inode.i_block[0] = 10;
    inode.i_block[1] = 11;
    inode.i_block[2] = 12;
    // Block 3 is sparse
    inode.i_block[4] = 15;

    // Setup indirect blocks (logical 12 onwards)
    uint32_t indirect_block = 100; // Physical block 100
    inode.i_block[12] = indirect_block;

    uint32_t *indirect_data = (uint32_t *)(mock_disk + (indirect_block * fs.block_size));
    indirect_data[0] = 20; // logical 12 -> phys 20
    indirect_data[1] = 21; // logical 13 -> phys 21
    indirect_data[2] = 22; // logical 14 -> phys 22
    indirect_data[3] = 0;  // logical 15 -> sparse
    indirect_data[4] = 24; // logical 16 -> phys 24

    uint32_t *indirect_buf = malloc(fs.block_size);
    uint32_t *dindirect_buf = malloc(fs.block_size);
    uint32_t *tindirect_buf = malloc(fs.block_size);

    uint32_t phys_block = 0;
    uint32_t count = 0;

    // Test Case 1: max_count == 0
    ext2_get_blocks_extent(&fs, &inode, 0, 0, &phys_block, &count, indirect_buf, dindirect_buf, tindirect_buf);
    assert(phys_block == 0);
    assert(count == 0);

    // Test Case 2: Contiguous direct blocks
    ext2_get_blocks_extent(&fs, &inode, 0, 4, &phys_block, &count, indirect_buf, dindirect_buf, tindirect_buf);
    assert(phys_block == 10);
    assert(count == 3); // blocks 0, 1, 2 are contiguous, block 3 is 0

    // Test Case 3: Sparse direct block
    ext2_get_blocks_extent(&fs, &inode, 3, 2, &phys_block, &count, indirect_buf, dindirect_buf, tindirect_buf);
    assert(phys_block == 0);
    assert(count == 1); // block 3 is 0, block 4 is 15

    // Test Case 4: Contiguous indirect blocks
    ext2_get_blocks_extent(&fs, &inode, 12, 4, &phys_block, &count, indirect_buf, dindirect_buf, tindirect_buf);
    assert(phys_block == 20);
    assert(count == 3); // blocks 12, 13, 14 are contiguous, block 15 is 0

    // Test Case 5: Sparse indirect block
    ext2_get_blocks_extent(&fs, &inode, 15, 2, &phys_block, &count, indirect_buf, dindirect_buf, tindirect_buf);
    assert(phys_block == 0);
    assert(count == 1); // block 15 is 0, block 16 is 24

    free(indirect_buf);
    free(dindirect_buf);
    free(tindirect_buf);
    free(mock_disk);

    printf("All test cases passed!\n");

    return 0;
}
