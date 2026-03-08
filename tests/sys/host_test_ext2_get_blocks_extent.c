#include <assert.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef long off_t;

void kprint(const char *str) {
    (void)str;
}

#include <sys/lock.h>
void spinlock_init(spinlock_t *lock, const char *name) { (void)lock; (void)name; }
void spinlock_acquire(spinlock_t *lock) { (void)lock; }
void spinlock_release(spinlock_t *lock) { (void)lock; }
bool spinlock_is_held(spinlock_t *lock) { (void)lock; return false; }
void mutex_init(mutex_t *m, const char *name) { m->locked = 0; m->name = name; }
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

#include <vm/uma.h>
void uma_startup(void) {}
void uma_enable_dynamic_alloc(void) {}
uma_zone_t *uma_zcreate(const char *name, size_t size, uma_ctor ctor, uma_dtor dtor,
                        uma_init init, uma_fini fini, int align, uint32_t flags) {
    (void)ctor; (void)dtor; (void)init; (void)fini; (void)align; (void)flags;
    uma_zone_t *z = (uma_zone_t *)malloc(sizeof(uma_zone_t));
    if (!z) return NULL;
    memset(z, 0, sizeof(*z));
    z->uz_name = name;
    z->uz_size = size;
    return z;
}
void uma_zdestroy(uma_zone_t *zone) { free(zone); }
void *uma_zalloc(uma_zone_t *zone, int flags) {
    (void)flags;
    return calloc(1, zone->uz_size > 0 ? zone->uz_size : 4096);
}
void uma_zfree(uma_zone_t *zone, void *item) {
    (void)zone;
    free(item);
}

#define vasprintf kernel_vasprintf
int kernel_vasprintf(char **strp, const char *fmt, va_list ap) {
    (void)strp; (void)fmt; (void)ap;
    return 0;
}

static uint8_t *mock_disk;
static size_t mock_disk_size = 1024 * 1024;

size_t mock_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    if ((size_t)offset + size > mock_disk_size) return 0;
    memcpy(buffer, mock_disk + offset, size);
    return size;
}

size_t mock_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node;
    if ((size_t)offset + size > mock_disk_size) return 0;
    memcpy(mock_disk + offset, buffer, size);
    return size;
}

#include <fs/ext2/ext2.h>
void ext2_init(void);
uint32_t ext2_get_block_num(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t block_idx,
                            uint32_t *indirect_buf, uint32_t *dindirect_buf, uint32_t *tindirect_buf);

int main(void) {
    mock_disk = calloc(1, mock_disk_size);
    if (!mock_disk) return 1;

    ext2_fs_t fs;
    memset(&fs, 0, sizeof(fs));

    fs_node_t dev_node;
    memset(&dev_node, 0, sizeof(dev_node));
    dev_node.read = mock_read;
    dev_node.write = mock_write;

    fs.device = &dev_node;
    fs.block_size = 1024;
    fs.inodes_per_group = 100;
    fs.blocks_per_group = 10000;

    ext2_init();

    ext2_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.i_block[0] = 10;
    inode.i_block[1] = 11;
    inode.i_block[2] = 12;
    inode.i_block[4] = 15;
    inode.i_block[12] = 100;

    uint32_t *indirect_data = (uint32_t *)(mock_disk + (100 * fs.block_size));
    indirect_data[0] = 20;
    indirect_data[1] = 21;
    indirect_data[2] = 22;
    indirect_data[3] = 0;
    indirect_data[4] = 24;

    uint32_t *indirect_buf = malloc(fs.block_size);
    uint32_t *dindirect_buf = malloc(fs.block_size);
    uint32_t *tindirect_buf = malloc(fs.block_size);
    uint32_t phys_block = 0;
    uint32_t count = 0;

    ext2_get_blocks_extent(&fs, &inode, 0, 0, &phys_block, &count, indirect_buf, dindirect_buf, tindirect_buf);
    assert(phys_block == 0 && count == 0);

    ext2_get_blocks_extent(&fs, &inode, 0, 4, &phys_block, &count, indirect_buf, dindirect_buf, tindirect_buf);
    assert(phys_block == 10 && count == 3);

    ext2_get_blocks_extent(&fs, &inode, 3, 2, &phys_block, &count, indirect_buf, dindirect_buf, tindirect_buf);
    assert(phys_block == 0 && count == 1);

    ext2_get_blocks_extent(&fs, &inode, 12, 4, &phys_block, &count, indirect_buf, dindirect_buf, tindirect_buf);
    assert(phys_block == 20 && count == 3);

    ext2_get_blocks_extent(&fs, &inode, 15, 2, &phys_block, &count, indirect_buf, dindirect_buf, tindirect_buf);
    assert(phys_block == 0 && count == 1);

    free(indirect_buf);
    free(dindirect_buf);
    free(tindirect_buf);
    free(mock_disk);

    puts("PASS: host_test_ext2_get_blocks_extent");
    return 0;
}
