// Reproduction test for Minix readdir buffer overflow (CVE-Like)
// Simulates reading a non-null-terminated directory entry from disk

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/mman.h>

// Mock standard library for kernel (if needed, but host headers suffice for most)
// We need to define types used in kernel headers if not present
// size_t, off_t are standard.

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
// Returns 1024 bytes (block size)
size_t read_fs(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    // We only care about directory content reading
    // minix_readdir calls read_fs to get entries.
    // It calculates offset = index * entry_size.
    // Index 0 -> Offset 0.

    // Return a block full of 'A's for the name of the first entry.
    // Entry format: uint16_t inode, char name[30]

    if (size > 0) {
        memset(buffer, 0, size);

        // Construct malicious entry at offset 0
        // We assume the caller asks for at least sizeof(entry)

        // Since minix_readdir reads entry by entry (32 bytes),
        // OR it reads a block.
        // In minix.c: minix_readdir calls minix_read.
        // minix_read calls read_fs(..., block_index * BLOCK_SIZE, ...).

        // Wait, minix_readdir calls minix_read.
        // minix_read calls read_fs.
        // We can just mock minix_read? No, minix_read is static in minix.c.
        // So we mock read_fs.

        // minix_read reads a whole block (1024) usually.
        // Let's see minix_read implementation in minix.c:
        // It calculates block_index, reads 1024 bytes into stack buffer `block_buf`.
        // Then copies requested chunk to user buffer.

        // So read_fs will be called with size=1024 (MINIX_BLOCK_SIZE).
        // We fill this buffer.

        if (size >= 32) {
            uint16_t inode = 1;
            memcpy(buffer, &inode, 2);
            // Fill name with 30 'A's (no null)
            memset(buffer + 2, 'A', 30);

            // Fill the rest with non-null garbage to ensure strcpy keeps going
            // if it overreads.
            memset(buffer + 32, 'B', size - 32);
        }
        return size;
    }
    return 0;
}

size_t write_fs(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    return 0;
}

// Stubs for other VFS
void vfs_register_filesystem(filesystem_t *fs) { (void)fs; }

// Include the source
// We use relative path assuming compilation from root with -I.
// If compiling with -I., verify path.
#include "sys/fs/minix/minix.c"

// Mock time
int64_t get_time(void) {
    return 1234567890;
}

// Helper to poison stack
void poison_stack() {
    char buf[4096];
    memset(buf, 'X', sizeof(buf));
    // Prevent optimization
    volatile char *p = buf;
    (void)p;
}

int main() {
    printf("Starting Minix Vuln Test...\n");

    // Setup a mock node
    fs_node_t node;
    memset(&node, 0, sizeof(node));
    node.flags = FS_DIRECTORY;
    node.length = 1024; // Big enough for one block

    // We need minix_fs_t for node->impl
    minix_fs_t fs;
    memset(&fs, 0, sizeof(fs));
    fs.sb.s_magic = MINIX_V1_Magic; // Standard V1
    fs.block_device = &node; // Circular ref for test

    node.impl = (uintptr_t)&fs;
    node.ptr = NULL; // No cache needed for readdir usually?
    // minix_readdir doesn't use node->ptr (inode cache) directly for reading logic?
    // Let's check minix_readdir in minix.c
    // It calls minix_read(node, ...).
    // minix_read casts node->impl to fs.
    // It uses fs->block_device.
    // It calls read_fs(fs->block_device, ...).

    // Also minix_get_zone is called by minix_read.
    // minix_get_zone checks node->ptr.
    // If node->ptr is NULL, it returns 0 (hole).
    // So we need node->ptr to be valid inode.

    struct minix_inode_v1 inode_v1;
    memset(&inode_v1, 0, sizeof(inode_v1));
    // Direct zones must be non-zero to read data.
    inode_v1.i_zone[0] = 10; // Dummy zone 10

    node.ptr = (struct fs_node *)&inode_v1; // Cast to void* or fs_node*?
    // minix.c casts to struct minix_inode_v1* inside minix_get_zone.

    // Now call readdir
    // Poison stack first
    poison_stack();

    struct dirent *d = minix_readdir(&node, 0);

    if (!d) {
        printf("FAIL: minix_readdir returned NULL\n");
        return 1;
    }

    printf("Read entry: name='%.35s' (len=%lu)\n", d->name, strlen(d->name));

    if (strlen(d->name) > 30) {
        printf("VULNERABLE: Name length %lu > 30. Stack overflow detected!\n", strlen(d->name));
        printf("Content after 30 chars: %02X %02X ...\n", (unsigned char)d->name[30], (unsigned char)d->name[31]);
    } else {
        printf("SAFE: Name length %lu <= 30.\n", strlen(d->name));
    }

    return 0;
}
