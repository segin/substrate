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

int64_t get_time(void) {
    return 0;
}

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

    uint32_t indirect_buf[256];
    memset(indirect_buf, 0, sizeof(indirect_buf));

    printf("Test 1: Allocating direct block 5\n");
    int ret = ext2_alloc_inode_block(&fs, &inode, 5, indirect_buf);
    if (ret != 0 || inode.i_block[5] == 0) {
        printf("FAILED: direct block allocation ret=%d block=%u\n", ret, inode.i_block[5]);
        failed++;
    }

    printf("Test 2: Allocating indirect block 12\n");
    ret = ext2_alloc_inode_block(&fs, &inode, 12, indirect_buf);
    if (ret != 0 || inode.i_block[12] == 0 || indirect_buf[0] == 0) {
        printf("FAILED: indirect allocation ret=%d indirect=%u data=%u\n", ret, inode.i_block[12], indirect_buf[0]);
        failed++;
    }

    printf("Test 3: Allocating indirect block 13\n");
    ret = ext2_alloc_inode_block(&fs, &inode, 13, indirect_buf);
    if (ret != 0 || indirect_buf[1] == 0) {
        printf("FAILED: second indirect allocation ret=%d data=%u\n", ret, indirect_buf[1]);
        failed++;
    }

    printf("Test 4: Allocating double indirect block (unsupported)\n");
    uint32_t ptrs_per_block = fs.block_size / 4;
    ret = ext2_alloc_inode_block(&fs, &inode, 12 + ptrs_per_block, indirect_buf);
    if (ret != -1) {
        printf("FAILED: double indirect returned %d\n", ret);
        failed++;
    }

    printf("Test 5: Allocating when no free blocks\n");
    bgd[0].bg_free_blocks_count = 0;
    fs.sb.s_free_blocks_count = 0;
    memset(active_bitmap, 0xFF, sizeof(active_bitmap));
    memcpy(mock_disk + 1024, active_bitmap, 1024);
    ret = ext2_alloc_inode_block(&fs, &inode, 6, indirect_buf);
    if (ret != -1) {
        printf("FAILED: out-of-space returned %d\n", ret);
        failed++;
    }

    if (failed != 0) {
        printf("%d tests FAILED!\n", failed);
        return 1;
    }

    printf("All tests PASSED!\n");
    return 0;
}
