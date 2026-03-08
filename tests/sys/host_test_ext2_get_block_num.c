#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef long off_t;

void kprint(const char *str) {
    // printf("%s", str);
}
int kprintf(const char *fmt, ...) {
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
    return NULL;
}
void *uma_zalloc(uma_zone_t *zone, int flags) {
    return malloc(1024);
}
void uma_zfree(uma_zone_t *zone, void *item) {
    free(item);
}


#include <vfs/vfs.h>
void vfs_register_filesystem(filesystem_t *fs) {
}

#include <sys/lock.h>
void mutex_init(mutex_t *m, const char *name) {}
void mutex_lock(mutex_t *m) {}
void mutex_unlock(mutex_t *m) {}

int64_t get_time(void) {
    return 0;
}

#define vasprintf kernel_vasprintf

#include "../../sys/fs/ext2/ext2.c"

#define ASSERT_EQ(a, b) do { if ((a) != (b)) { printf("ASSERT_EQ failed at line %d: %d != %d\n", __LINE__, (int)(a), (int)(b)); exit(1); } } while(0)
#define ASSERT_TRUE(a) do { if (!(a)) { printf("ASSERT_TRUE failed at line %d\n", __LINE__); exit(1); } } while(0)

int read_block_call_count = 0;
uint32_t last_read_block = 0;

size_t test_device_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    read_block_call_count++;
    uint32_t block_num = offset / 1024;
    last_read_block = block_num;
    memset(buffer, 0, size);

    // Simulate indirect block data
    uint32_t *ptrs = (uint32_t*)buffer;
    if (block_num == 100) {
        ptrs[5] = 205; // Indirect block points to 205 at index 5
    } else if (block_num == 200) {
        ptrs[3] = 300; // Dindirect points to 300 at index 3
    } else if (block_num == 300) {
        ptrs[7] = 307; // Indirect (from dindirect) points to 307 at index 7
    } else if (block_num == 400) {
        ptrs[2] = 500; // Tindirect points to 500
    } else if (block_num == 500) {
        ptrs[4] = 600; // Dindirect points to 600
    } else if (block_num == 600) {
        ptrs[6] = 606; // Indirect points to 606
    }

    return size;
}

int main() {
    printf("Testing ext2_get_block_num...\n");

    ext2_fs_t fs;
    memset(&fs, 0, sizeof(fs));
    fs.block_size = 1024;

    fs_node_t dev;
    memset(&dev, 0, sizeof(dev));
    dev.read = test_device_read;
    fs.device = &dev;

    ext2_inode_t inode;
    memset(&inode, 0, sizeof(inode));

    uint32_t indirect_buf[256];
    uint32_t dindirect_buf[256];
    uint32_t tindirect_buf[256];

    // Test 1: Direct blocks
    inode.i_block[0] = 10;
    inode.i_block[11] = 21;

    ASSERT_EQ(ext2_get_block_num(&fs, &inode, 0, indirect_buf, dindirect_buf, tindirect_buf), 10);
    ASSERT_EQ(ext2_get_block_num(&fs, &inode, 11, indirect_buf, dindirect_buf, tindirect_buf), 21);

    // Test 2: Indirect blocks
    // ptrs_per_block = 1024 / 4 = 256
    // block_idx = 12 + 5 = 17
    inode.i_block[12] = 100;

    read_block_call_count = 0;
    ASSERT_EQ(ext2_get_block_num(&fs, &inode, 17, indirect_buf, dindirect_buf, tindirect_buf), 205);
    ASSERT_EQ(read_block_call_count, 1);
    ASSERT_EQ(last_read_block, 100);

    // Test 3: Double indirect blocks
    // block_idx = 12 + 256 + (3 * 256 + 7) = 268 + 768 + 7 = 1043
    inode.i_block[13] = 200;

    read_block_call_count = 0;
    ASSERT_EQ(ext2_get_block_num(&fs, &inode, 1043, indirect_buf, dindirect_buf, tindirect_buf), 307);
    ASSERT_EQ(read_block_call_count, 2);
    ASSERT_EQ(last_read_block, 300); // The second read is the indirect block

    // Test 4: Triple indirect blocks
    // block_idx = 12 + 256 + 256*256 + (2 * 256*256 + 4 * 256 + 6)
    // = 268 + 65536 + (131072 + 1024 + 6) = 65804 + 132102 = 197906
    inode.i_block[14] = 400;

    read_block_call_count = 0;
    ASSERT_EQ(ext2_get_block_num(&fs, &inode, 197906, indirect_buf, dindirect_buf, tindirect_buf), 606);
    ASSERT_EQ(read_block_call_count, 3);

    // Test 5: Out of bounds
    // > 12 + 256 + 256*256 + 256*256*256
    uint32_t max_blocks = 12 + 256 + 65536 + 16777216;
    ASSERT_EQ(ext2_get_block_num(&fs, &inode, max_blocks, indirect_buf, dindirect_buf, tindirect_buf), 0);

    // Test 6: Missing indirect blocks
    inode.i_block[12] = 0;
    ASSERT_EQ(ext2_get_block_num(&fs, &inode, 17, indirect_buf, dindirect_buf, tindirect_buf), 0);

    printf("All ext2_get_block_num tests passed!\n");
    return 0;
}
