#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// ==========================================
// Mocks for Kernel Environment
// ==========================================

typedef long off_t;

void kprint(const char *str) {
    // printf("%s", str); // Silence kprint for test output clarity
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

void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

// Mock Device Implementation
// We simulate a block device returning a directory block
static uint8_t mock_block_data[1024];

size_t mock_device_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    if (size <= 1024) {
        memcpy(buffer, mock_block_data, size);
    }
    return size;
}

size_t mock_device_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    return size;
}

int64_t get_time(void) {
    return 0;
}

// Include Driver Source
#define vasprintf kernel_vasprintf
#include "../../sys/fs/ext2/ext2.c"

int main() {
    printf("Running Ext2 Security Vulnerability Reproduction...\n");

    // 1. Setup Filesystem
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

    // 2. Setup Inode for a directory
    ext2_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.i_size = 1024;
    inode.i_mode = EXT2_S_IFDIR;
    inode.i_block[0] = 100;

    // 3. Craft the malicious directory entry in mock_block_data
    memset(mock_block_data, 0, 1024);

    ext2_dirent_t *de = (ext2_dirent_t *)mock_block_data;
    de->inode = 0x12345678;
    de->rec_len = 1024;

    // VULNERABILITY: Name length > 128
    de->name_len = 200;
    memset(de->name, 'A', 200);

    // 4. Create the node context manually
    ext2_node_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.fs = &fs;
    ctx.inode_num = 10;
    memcpy(&ctx.inode, &inode, sizeof(ext2_inode_t));

    // Poison the current_dirent.ino
    ctx.current_dirent.ino = 0xDEADBEEFCAFEBABE;

    fs_node_t node;
    memset(&node, 0, sizeof(node));
    node.impl = (uintptr_t)&ctx;

    // 5. Trigger readdir
    struct dirent *result = ext2_readdir(&node, 0);

    if (result == NULL) {
        printf("FAILED: readdir returned NULL\n");
        return 1;
    }

    // Check for overflow
    // ino should be 0x12345678 (set by ext2_readdir)
    // and NOT overwritten by 'A's

    if (ctx.current_dirent.ino == 0x12345678) {
        // Also check if name is truncated correctly
        size_t len = strlen(result->name);
        if (len < 128) {
             printf("SUCCESS: Buffer Overflow Prevented. Name length: %zu\n", len);
             return 0;
        } else {
             printf("FAILED: Name length %zu >= 128 (Not truncated?)\n", len);
             return 1;
        }
    } else if ((ctx.current_dirent.ino & 0xFF) == 'A') {
         printf("VULNERABILITY CONFIRMED: ino was overwritten by name overflow!\n");
         return 1;
    } else {
        printf("FAILED: Unexpected state. ino: 0x%lx\n", ctx.current_dirent.ino);
        return 1;
    }

    return 0;
}
