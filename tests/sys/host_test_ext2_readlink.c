#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

#include <sys/lock.h>
void mutex_init(mutex_t *m, const char *name) { (void)m; (void)name; }
void mutex_lock(mutex_t *m) { (void)m; }
void mutex_unlock(mutex_t *m) { (void)m; }

#include <vm/uma.h>
uma_zone_t *uma_zcreate(const char *name, size_t size,
                        int (*ctor)(void *, int, void *, int),
                        void (*dtor)(void *, int, void *),
                        int (*init)(void *, int, int),
                        void (*fini)(void *, int),
                        int align, uint32_t flags) {
    (void)name; (void)size; (void)ctor; (void)dtor; (void)init; (void)fini; (void)align; (void)flags;
    return (uma_zone_t *)1;
}
void *uma_zalloc(uma_zone_t *zone, int flags) {
    (void)zone; (void)flags;
    return calloc(1, 1024);
}
void uma_zfree(uma_zone_t *zone, void *item) {
    (void)zone;
    free(item);
}

int64_t get_time(void) { return 0; }

#include <vfs/vfs.h>
void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

static uint8_t mock_block_data[1024 * 10];

size_t mock_device_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    if (offset < 0 || (size_t)offset >= sizeof(mock_block_data)) {
        return 0;
    }
    size_t available = sizeof(mock_block_data) - (size_t)offset;
    if (size <= available) {
        memcpy(buffer, mock_block_data + offset, size);
        return size;
    }
    memcpy(buffer, mock_block_data + offset, available);
    return available;
}

size_t mock_device_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    return size;
}

#include "../../sys/fs/ext2/ext2.c"

int main(void) {
    ext2_fs_t fs;
    memset(&fs, 0, sizeof(fs));
    fs.block_size = 1024;

    fs_node_t mock_dev;
    memset(&mock_dev, 0, sizeof(mock_dev));
    mock_dev.read = mock_device_read;
    mock_dev.write = mock_device_write;
    fs.device = &mock_dev;

    ext2_inode_t inode;
    memset(&inode, 0, sizeof(inode));

    ext2_node_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.fs = &fs;
    ctx.inode_num = 10;

    fs_node_t node;
    memset(&node, 0, sizeof(node));
    node.impl = (uintptr_t)&ctx;

    const char *fast_target = "fast_symlink_target";
    inode.i_size = (uint32_t)strlen(fast_target);
    memcpy(inode.i_block, fast_target, inode.i_size);
    memcpy(&ctx.inode, &inode, sizeof(ext2_inode_t));

    char buf[128];
    uint32_t len = ext2_readlink(&node, buf, sizeof(buf));
    if (len != inode.i_size || strcmp(buf, fast_target) != 0) {
        printf("FAILED: fast symlink read len=%u buf=%s\n", len, buf);
        return 1;
    }

    const char *slow_target = "this_is_a_very_long_symlink_target_that_exceeds_sixty_characters_and_therefore_is_stored_in_data_blocks";
    inode.i_size = (uint32_t)strlen(slow_target);
    memset(inode.i_block, 0, sizeof(inode.i_block));
    inode.i_block[0] = 1;
    memcpy(&ctx.inode, &inode, sizeof(ext2_inode_t));
    memcpy(mock_block_data + fs.block_size, slow_target, inode.i_size);

    len = ext2_readlink(&node, buf, sizeof(buf));
    if (len != inode.i_size || strcmp(buf, slow_target) != 0) {
        printf("FAILED: slow symlink read len=%u expected=%u buf=%s\n", len, inode.i_size, buf);
        return 1;
    }

    len = ext2_readlink(&node, buf, 10);
    if (len != 9 || strncmp(buf, slow_target, 9) != 0 || buf[9] != '\0') {
        printf("FAILED: truncated slow symlink len=%u buf=%s\n", len, buf);
        return 1;
    }

    printf("PASS: host_test_ext2_readlink\n");
    return 0;
}
