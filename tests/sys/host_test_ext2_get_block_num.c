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
int kprintf(const char *fmt, ...) {
    (void)fmt;
    return 0;
}

#include <vm/vm_kmem.h>
void *kmalloc(size_t size) {
    return calloc(1, size);
}
void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

#include <vm/uma.h>
uma_zone_t *uma_zcreate(const char *name, size_t size, uma_ctor ctor, uma_dtor dtor, uma_init uinit, uma_fini ufini, int align, uint32_t flags) {
    (void)name; (void)size; (void)ctor; (void)dtor; (void)uinit; (void)ufini; (void)align; (void)flags;
    return NULL;
}
void *uma_zalloc(uma_zone_t *zone, int flags) {
    (void)zone; (void)flags;
    return malloc(1024);
}
void uma_zfree(uma_zone_t *zone, void *item) {
    (void)zone;
    free(item);
}

#include <vfs/vfs.h>
void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

#include <sys/lock.h>
void mutex_init(mutex_t *m, const char *name) { (void)m; (void)name; }
void mutex_lock(mutex_t *m) { (void)m; }
void mutex_unlock(mutex_t *m) { (void)m; }

int64_t get_time(void) {
    return 0;
}

// ==========================================
// Stubs for kernel subsystems ext2.c grew calls into
// ==========================================
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

#define ASSERT_EQ(a, b) do { if ((a) != (b)) { printf("ASSERT_EQ failed at line %d: %d != %d\n", __LINE__, (int)(a), (int)(b)); exit(1); } } while (0)

static int read_block_call_count;
static uint32_t last_read_block;

size_t test_device_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    read_block_call_count++;
    uint32_t block_num = (uint32_t)(offset / 1024);
    last_read_block = block_num;
    memset(buffer, 0, size);

    uint32_t *ptrs = (uint32_t *)buffer;
    if (block_num == 100) {
        ptrs[5] = 205;
    } else if (block_num == 200) {
        ptrs[3] = 300;
    } else if (block_num == 300) {
        ptrs[7] = 307;
    } else if (block_num == 400) {
        ptrs[2] = 500;
    } else if (block_num == 500) {
        ptrs[4] = 600;
    } else if (block_num == 600) {
        ptrs[6] = 606;
    }

    return size;
}

int main(void) {
    ext2_fs_t fs;
    memset(&fs, 0, sizeof(fs));
    fs.block_size = 1024;
    /* ext2_read_block (reached through ext2_get_block_num's indirect
     * lookups) now rejects block_num >= s_blocks_count.  Make the
     * filesystem span every block the mock device serves. */
    fs.sb.s_blocks_count = 0xFFFFFFFFu;

    fs_node_t dev;
    memset(&dev, 0, sizeof(dev));
    dev.read = test_device_read;
    fs.device = &dev;

    ext2_inode_t inode;
    memset(&inode, 0, sizeof(inode));

    uint32_t indirect_buf[256];
    uint32_t dindirect_buf[256];
    uint32_t tindirect_buf[256];

    inode.i_block[0] = 10;
    inode.i_block[11] = 21;
    ASSERT_EQ(ext2_get_block_num(&fs, &inode, 0, indirect_buf, dindirect_buf, tindirect_buf), 10);
    ASSERT_EQ(ext2_get_block_num(&fs, &inode, 11, indirect_buf, dindirect_buf, tindirect_buf), 21);

    inode.i_block[12] = 100;
    read_block_call_count = 0;
    ASSERT_EQ(ext2_get_block_num(&fs, &inode, 17, indirect_buf, dindirect_buf, tindirect_buf), 205);
    ASSERT_EQ(read_block_call_count, 1);
    ASSERT_EQ(last_read_block, 100);

    inode.i_block[13] = 200;
    read_block_call_count = 0;
    ASSERT_EQ(ext2_get_block_num(&fs, &inode, 1043, indirect_buf, dindirect_buf, tindirect_buf), 307);
    ASSERT_EQ(read_block_call_count, 2);
    ASSERT_EQ(last_read_block, 300);

    inode.i_block[14] = 400;
    read_block_call_count = 0;
    ASSERT_EQ(ext2_get_block_num(&fs, &inode, 197906, indirect_buf, dindirect_buf, tindirect_buf), 606);
    ASSERT_EQ(read_block_call_count, 3);

    uint32_t max_blocks = 12 + 256 + 65536 + 16777216;
    ASSERT_EQ(ext2_get_block_num(&fs, &inode, max_blocks, indirect_buf, dindirect_buf, tindirect_buf), 0);

    inode.i_block[12] = 0;
    ASSERT_EQ(ext2_get_block_num(&fs, &inode, 17, indirect_buf, dindirect_buf, tindirect_buf), 0);

    puts("PASS: host_test_ext2_get_block_num");
    return 0;
}
