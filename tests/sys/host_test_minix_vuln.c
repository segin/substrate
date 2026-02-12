// Reproduction test for Minix readdir buffer overflow (CVE-Like)
// Simulates reading a non-null-terminated directory entry from disk

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Mock Kernel Functions
void kprint(const char *s) {
    printf("[KERNEL] %s", s);
}

void *kmalloc(size_t size) {
    return malloc(size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

// Include VFS header to get types
#include <vfs/vfs.h>

// Mock read_fs
size_t read_fs(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    if (size > 0) {
        memset(buffer, 0, size);
        if (size >= 32) {
            uint16_t inode = 1;
            memcpy(buffer, &inode, 2);
            // Fill name with 30 'A's (no null)
            memset(buffer + 2, 'A', 30);
            // Fill the rest with garbage
            if (size > 32) memset(buffer + 32, 'B', size - 32);
        }
        return size;
    }
    return 0;
}

size_t write_fs(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    return 0;
}

void vfs_register_filesystem(filesystem_t *fs) { (void)fs; }

// Include the real source
#include "../../sys/fs/minix/minix.c"

// Mock time
int64_t get_time(void) {
    return 1234567890;
}

// Helper to poison stack
void poison_stack() {
    char buf[4096];
    memset(buf, 'X', sizeof(buf));
    volatile char *p = buf;
    (void)p;
}

int main() {
    printf("Starting Minix Vuln Test (Real Code)...\n");

    fs_node_t node;
    memset(&node, 0, sizeof(node));
    node.flags = FS_DIRECTORY;
    node.length = 1024;

    minix_fs_t fs;
    memset(&fs, 0, sizeof(fs));
    fs.sb.s_magic = MINIX_V1_Magic;
    fs.block_device = &node;

    node.impl = (uintptr_t)&fs;
    
    struct minix_inode_v1 inode_v1;
    memset(&inode_v1, 0, sizeof(inode_v1));
    inode_v1.i_zone[0] = 10;
    node.ptr = (struct fs_node *)&inode_v1;

    poison_stack();

    struct dirent *d = minix_readdir(&node, 0);

    if (!d) {
        printf("FAIL: minix_readdir returned NULL\n");
        return 1;
    }

    printf("Read entry: d_name='%.35s' (len=%lu)\n", d->d_name, strlen(d->d_name));

    if (strlen(d->d_name) > 30) {
        printf("VULNERABLE: d_name length %lu > 30. Stack over-read detected!\n", strlen(d->d_name));
        return 1;
    } else {
        printf("SAFE: d_name length %lu <= 30.\n", strlen(d->d_name));
    }

    return 0;
}
