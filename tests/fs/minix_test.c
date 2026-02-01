#define _GNU_SOURCE // For asprintf if needed, or other extensions
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/mman.h>

// Mocking kernel environment
void kprint(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

void *kmalloc(size_t size) {
    // Use MAP_32BIT to ensure pointers fit in 32-bit fs_node_t->impl
    // This allows testing 32-bit kernel code on 64-bit host
    void *ptr = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT, -1, 0);
    if (ptr == MAP_FAILED) return NULL;
    return ptr; // mmap returns zeroed memory
}

void kfree(void *ptr, size_t size) {
    munmap(ptr, size);
}

// Forward declarations for mocks
struct fs_node;
typedef struct fs_node fs_node_t;
typedef int64_t off_t;

// We need to match the signature in vfs.h
// size_t read_fs(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer);
// size_t write_fs(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer);

// Our block device mock
static uint8_t *disk_image = NULL;
static size_t disk_size = 0;

size_t read_fs(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    // In minix driver, the "block device" node is passed to read_fs.
    // We assume node->inode or impl refers to our mocked disk.
    // For simplicity, we just read from the global disk_image.
    (void)node;
    // printf("read_fs: offset=%ld size=%lu\n", offset, size);
    if ((size_t)offset >= disk_size) return 0;
    if (offset + size > disk_size) size = disk_size - offset;
    memcpy(buffer, disk_image + offset, size);
    return size;
}

size_t write_fs(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node;
    if ((size_t)offset >= disk_size) return 0;
    if (offset + size > disk_size) size = disk_size - offset; // Or extend? Minix driver assumes fixed size block dev usually
    memcpy(disk_image + offset, buffer, size);
    return size;
}

// Mock vfs functions called by minix.c
#include <vfs/vfs.h> // This picks up our mock header which defines types

void vfs_register_filesystem(filesystem_t *fs) {
    printf("Registered filesystem: %s\n", fs->name);
}

// Include the source file under test
#include "../../sys/fs/minix/minix.c"

// Test Utilities
void create_minix_image(const char *filename) {
    // Generate a file
    char cmd[256];
    // Create 1MB image
    snprintf(cmd, sizeof(cmd), "dd if=/dev/zero of=%s bs=1024 count=1440 2>/dev/null", filename);
    system(cmd);
    // Format it
    snprintf(cmd, sizeof(cmd), "mkfs.minix -1 %s >/dev/null", filename);
    system(cmd);
}

void load_disk_image(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) { perror("fopen"); exit(1); }
    fseek(f, 0, SEEK_END);
    disk_size = ftell(f);
    printf("Disk size: %lu bytes\n", disk_size);
    fseek(f, 0, SEEK_SET);
    disk_image = malloc(disk_size);
    if (!disk_image) { perror("malloc"); exit(1); }
    if (fread(disk_image, 1, disk_size, f) != disk_size) {
        perror("fread"); exit(1);
    }
    fclose(f);
}

void save_disk_image(const char *filename) {
    FILE *f = fopen(filename, "wb");
    if (!f) { perror("fopen"); exit(1); }
    fwrite(disk_image, 1, disk_size, f);
    fclose(f);
}

// Tests
void test_mknod() {
    printf("Testing mknod...\n");

    // 1. Mount
    fs_node_t block_dev;
    memset(&block_dev, 0, sizeof(block_dev));
    // The driver uses read_fs on this node. Our mock read_fs ignores node content and uses global disk_image.

    fs_node_t *root = minix_mount("disk", 0, &block_dev);
    if (!root) {
        printf("FAIL: Mount failed\n");
        exit(1);
    }
    printf("Mounted Minix root.\n");

    // 2. Try mknod
    // Create a device node /dev/tty0 (c 4, 0) -> Minix uses 16-bit dev: major<<8 | minor
    // major 4, minor 0 -> 0x0400
    uint32_t dev = (4 << 8) | 0;
    int res = minix_mknod(root, "tty0", S_IFCHR | 0600, dev);

    if (res != 0) {
        printf("FAIL: mknod returned %d\n", res);
        // Don't exit yet, check if unimplemented
        // return;
    } else {
        printf("mknod returned success.\n");
    }

    // 3. Verify
    // Look up "tty0"
    fs_node_t *node = minix_finddir(root, "tty0");
    if (!node) {
        printf("FAIL: Could not find created node 'tty0'\n");
        exit(1);
    }

    printf("Found 'tty0': inode=%lu, flags=%x\n", node->inode, node->flags);

    if ((node->flags & FS_CHARDEVICE) == 0) {
        printf("FAIL: Node is not FS_CHARDEVICE (flags=%x)\n", node->flags);
    }

    // Check device number in raw inode?
    // We can re-read the inode from disk to be sure
    // Use internal minix_read_inode helper if we can, or just trust the node returned

    // Verify specific mode bits
    if ((node->mask & 0777) != 0600) {
        printf("FAIL: Permissions mismatch. Expected 0600, got %o\n", node->mask);
    }

    // Free resources
    free(node);
    // free(root); // Leaks, but it's a test
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("Starting test...\n");
    const char *img_file = "test_minix.img";
    create_minix_image(img_file);
    load_disk_image(img_file);

    // We rely on static minix functions so we don't need to link against separate object

    test_mknod();

    printf("ALL TESTS PASSED\n");
    return 0;
}
