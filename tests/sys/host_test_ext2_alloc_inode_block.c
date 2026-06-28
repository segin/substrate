#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef long off_t;

void kprint(const char *str) {
    (void)str;
}

#include <vm/vm_kmem.h>
void *kmalloc(size_t size) {
    return calloc(1, size);
}
void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

#include <vfs/vfs.h>
#include <sys/lock.h>

void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

void mutex_lock(mutex_t *m) { (void)m; }
void mutex_unlock(mutex_t *m) { (void)m; }
void mutex_init(mutex_t *m, const char *name) { (void)m; (void)name; }

#include <vm/uma.h>
void *uma_zalloc(uma_zone_t *zone, int flags) { (void)zone; (void)flags; return calloc(1, 4096); }
void uma_zfree(uma_zone_t *zone, void *item) { (void)zone; free(item); }
uma_zone_t *uma_zcreate(
    const char *name, size_t size,
    int (*ctor)(void *mem, int size, void *arg, int flags),
    void (*dtor)(void *mem, int size, void *arg),
    int (*init)(void *mem, int size, int flags),
    void (*fini)(void *mem, int size),
    int align, uint32_t flags)
{
    (void)name; (void)size; (void)ctor; (void)dtor; (void)init; (void)fini; (void)align; (void)flags;
    return NULL;
}

static uint8_t mock_disk[4096 * 10];

size_t mock_device_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    if (offset + size > sizeof(mock_disk)) return 0;
    memcpy(buffer, mock_disk + offset, size);
    return size;
}

size_t mock_device_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node;
    if (offset + size > sizeof(mock_disk)) return 0;
    memcpy(mock_disk + offset, buffer, size);
    return size;
}

long get_time(void) {
    return 0;
}

// ==========================================
// Stubs for kernel subsystems ext2.c grew calls into
// ==========================================
int kprintf(const char *fmt, ...) { (void)fmt; return 0; }
int cmdline_debug_enabled(const char *channel) { (void)channel; return 0; }

#include <crc32c.h>
uint32_t crc32c_update(uint32_t crc, const void *buf, size_t len) {
    (void)buf; (void)len; return crc;
}

#include <drivers/storage/blkdev.h>
size_t blkdev_read_bytes(blkdev_t *dev, uint64_t offset, size_t size, void *buffer) {
    (void)dev; (void)offset; (void)size; (void)buffer; return 0;
}

#include <fs/ext2/ext2.h>
int ext2_htree_hash(const char *name, int len, const uint32_t *hash_seed,
                    int hash_version, uint32_t *hash_major, uint32_t *hash_minor) {
    (void)name; (void)len; (void)hash_seed; (void)hash_version;
    if (hash_major) *hash_major = 0;
    if (hash_minor) *hash_minor = 0;
    return 0;
}
int ext2_xattr_get(fs_node_t *node, const char *full_name,
                   void *out, size_t out_size, size_t *result_size) {
    (void)node; (void)full_name; (void)out; (void)out_size;
    if (result_size) *result_size = 0;
    return -1; /* miss; xattr path is not exercised by this test */
}
int ext2_xattr_list(fs_node_t *node, void *out, size_t out_size, size_t *result_size) {
    (void)node; (void)out; (void)out_size;
    if (result_size) *result_size = 0;
    return 0;
}

unsigned long fs_open_count;
unsigned long fs_close_count;

#define vasprintf kernel_vasprintf
#include "../../sys/fs/ext2/ext2.c"

int main(void) {
    printf("Running Ext2 Alloc Inode Block Tests...\n");
    int failed = 0;

    ext2_fs_t fs;
    memset(&fs, 0, sizeof(fs));

    fs_node_t device_node;
    memset(&device_node, 0, sizeof(device_node));
    device_node.read = mock_device_read;
    device_node.write = mock_device_write;

    fs.device = &device_node;
    fs.block_size = 1024;
    fs.inodes_per_group = 100;
    fs.blocks_per_group = 10000;
    fs.sb.s_free_blocks_count = 1000;
    fs.sb.s_first_data_block = 1;

    ext2_group_desc_t bgd[1];
    memset(bgd, 0, sizeof(bgd));
    bgd[0].bg_free_blocks_count = 1000;
    bgd[0].bg_block_bitmap = 1;
    fs.bgd = bgd;
    fs.group_count = 1;

    uint8_t active_bitmap[1024];
    memset(active_bitmap, 0, sizeof(active_bitmap));
    fs.active_bg_bitmap = active_bitmap;
    fs.active_bg_group = 0;
    memcpy(mock_disk + 1024, active_bitmap, 1024);

    ext2_inode_t inode;
    memset(&inode, 0, sizeof(inode));

    uint32_t indirect[256];
    uint32_t dindirect[256];
    uint32_t tindirect[256];
    memset(indirect, 0, sizeof(indirect));
    memset(dindirect, 0, sizeof(dindirect));
    memset(tindirect, 0, sizeof(tindirect));

    printf("Test 1: Allocating direct block 5\n");
    int ret = ext2_alloc_inode_block(&fs, &inode, 5, indirect, dindirect, tindirect);
    if (ret != 0 || inode.i_block[5] == 0) {
        printf("FAILED: direct block allocation ret=%d block=%u\n", ret, inode.i_block[5]);
        failed++;
    }

    printf("Test 2: Allocating indirect block 12\n");
    ret = ext2_alloc_inode_block(&fs, &inode, 12, indirect, dindirect, tindirect);
    if (ret != 0 || inode.i_block[12] == 0 || indirect[0] == 0) {
        printf("FAILED: indirect allocation ret=%d indirect=%u data=%u\n", ret, inode.i_block[12], indirect[0]);
        failed++;
    }

    printf("Test 3: Allocating indirect block 13\n");
    ret = ext2_alloc_inode_block(&fs, &inode, 13, indirect, dindirect, tindirect);
    if (ret != 0 || indirect[1] == 0) {
        printf("FAILED: second indirect allocation ret=%d data=%u\n", ret, indirect[1]);
        failed++;
    }

    uint32_t ptrs_per_block = fs.block_size / 4;
    printf("Test 4: Allocating double indirect block\n");
    ret = ext2_alloc_inode_block(&fs, &inode, 12 + ptrs_per_block, indirect, dindirect, tindirect);
    if (ret != 0 || inode.i_block[13] == 0 || dindirect[0] == 0 || indirect[0] == 0) {
        printf("FAILED: double indirect ret=%d dindirect=%u indirect=%u\n", ret, inode.i_block[13], dindirect[0]);
        failed++;
    }

    printf("Test 5: Allocating triple indirect block\n");
    ret = ext2_alloc_inode_block(&fs, &inode, 12 + ptrs_per_block + ptrs_per_block * ptrs_per_block, indirect, dindirect, tindirect);
    if (ret != 0 || inode.i_block[14] == 0 || tindirect[0] == 0 || dindirect[0] == 0 || indirect[0] == 0) {
        printf("FAILED: triple indirect ret=%d tindirect=%u dindirect=%u indirect=%u\n", ret, inode.i_block[14], tindirect[0], dindirect[0]);
        failed++;
    }

    if (failed != 0) {
        printf("%d tests FAILED!\n", failed);
        return 1;
    }

    printf("All tests PASSED!\n");
    return 0;
}
