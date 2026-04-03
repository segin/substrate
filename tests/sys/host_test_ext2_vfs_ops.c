#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

// Mock Types
typedef long off_t;
void kprint(const char *str) { (void)str; }

#include <sys/lock.h>
void mutex_init(mutex_t *m, const char *name) { m->locked = 0; m->name = name; }
void mutex_lock(mutex_t *m) { m->locked = 1; }
void mutex_unlock(mutex_t *m) { m->locked = 0; }

#include <vm/vm_kmem.h>
void *kmalloc(size_t size) { return calloc(1, size); }
void kfree(void *ptr, size_t size) { (void)size; free(ptr); }

#include <vfs/vfs.h>
void vfs_register_filesystem(filesystem_t *fs) { (void)fs; }

#include <vm/uma.h>
uma_zone_t *uma_zcreate(const char *name, size_t size, uma_ctor ctor, uma_dtor dtor, uma_init init, uma_fini fini, int align, uint32_t flags) {
    uma_zone_t *z = calloc(1, sizeof(uma_zone_t));
    z->uz_name = name; z->uz_size = size; return z;
}
void uma_zdestroy(uma_zone_t *zone) { free(zone); }
void *uma_zalloc(uma_zone_t *zone, int flags) { return calloc(1, zone->uz_size > 0 ? zone->uz_size : 4096); }
void uma_zfree(uma_zone_t *zone, void *item) { free(item); }

static uint8_t mock_disk[1024 * 1024];
size_t mock_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    if (offset + size > sizeof(mock_disk)) return 0;
    memcpy(buffer, mock_disk + offset, size);
    return size;
}
size_t mock_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    if (offset + size > sizeof(mock_disk)) return 0;
    memcpy(mock_disk + offset, buffer, size);
    return size;
}

// Rename mock get_time to avoid conflict with kern/time.h if included by ext2.c
#define get_time mock_get_time
long mock_get_time(void) { return 123456789; }

#define vasprintf kernel_vasprintf
#include "../../sys/fs/ext2/ext2.c"

int main() {
    printf("Testing EXT2 VFS Ops (host-side)...\n");

    ext2_fs_t fs;
    memset(&fs, 0, sizeof(fs));
    fs.block_size = 1024;
    fs.sb.s_magic = EXT2_SUPER_MAGIC;
    fs.sb.s_blocks_count = 1000;
    fs.sb.s_free_blocks_count = 500;
    fs.sb.s_inodes_count = 100;
    fs.sb.s_free_inodes_count = 50;

    fs_node_t node;
    memset(&node, 0, sizeof(node));
    ext2_node_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.fs = &fs;
    node.impl = (uintptr_t)&ctx;

    struct statfs sfs;
    int ret = ext2_statfs(&node, &sfs);
    if (ret != 0) {
        printf("FAILED: ext2_statfs returned %d\n", ret);
        return 1;
    }
    if (sfs.f_type != EXT2_SUPER_MAGIC || sfs.f_blocks != 1000 || sfs.f_bfree != 500) {
        printf("FAILED: statfs data mismatch\n");
        return 1;
    }

    printf("SUCCESS: ext2_statfs test passed.\n");
    
    // Smoke check for link/rename (checking if they are callable)
    ret = ext2_link(&node, &node, "test");
    printf("ext2_link returned %d\n", ret);

    ret = ext2_rename(&node, "old", &node, "new");
    printf("ext2_rename returned %d\n", ret);

    return 0;
}
