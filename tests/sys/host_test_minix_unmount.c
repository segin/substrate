// Standalone test for Minix unmount
// Mocks necessary kernel environment

// Mock headers
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Mock functions needed by string.h/minix.c
void *memcpy(void *dest, const void *src, size_t n) {
    char *d = (char *)dest;
    const char *s = (const char *)src;
    while (n--) *d++ = *s++;
    return dest;
}

void *memset(void *s, int c, size_t n) {
    unsigned char *p = (unsigned char *)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

char *strcpy(char *dest, const char *src) {
    char *ret = dest;
    while ((*dest++ = *src++));
    return ret;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++; s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++)
        dest[i] = src[i];
    for ( ; i < n; i++)
        dest[i] = '\0';
    return dest;
}

int sprintf(char *str, const char *format, ...) {
    // Dummy implementation
    (void)format;
    *str = '\0';
    return 0;
}

// Mock Kernel Functions
int64_t get_time(void) {
    return 0;
}

void kprint(const char *s) {
    // Use host write if possible, or just ignore
    (void)s;
}

// Memory tracking and 32-bit allocation
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20
#define MAP_32BIT 0x40
#define MAP_FAILED ((void*)-1)

// mmap declaration. We use void* for last arg to avoid off_t mismatch issues, or long.
// On x86_64, off_t is 64-bit. size_t is 64-bit on host, 32-bit in headers.
// mmap expects unsigned long length.
// We'll declare it matching host expectation roughly but leniently.
extern void *mmap(void *addr, unsigned long length, int prot, int flags, int fd, long offset);
extern int munmap(void *addr, unsigned long length);

static int allocation_count = 0;
static int free_count = 0;

void *kmalloc(size_t size) {
    // Use mmap with MAP_32BIT to ensure pointer fits in 32-bit
    // size_t from kernel header is 32-bit. mmap takes 64-bit length.
    unsigned long len = size;
    // Round up to page size (4096)
    if (len == 0) return NULL;
    if (len < 4096) len = 4096;

    void *ptr = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (ptr == MAP_FAILED) return NULL;

    allocation_count++;
    return ptr;
}

void kfree(void *ptr, size_t size) {
    if (ptr) {
        free_count++;
        unsigned long len = size;
        if (len < 4096) len = 4096;
        munmap(ptr, len);
    }
}

// VFS Mocks
#include <vfs/vfs.h>

void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

// Read FS Mock
static uint8_t mock_sb[1024];
fs_node_t *mock_dev_node;

size_t read_fs(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    if (node == mock_dev_node) {
        if (offset == 1024 && size <= 1024) {
            memcpy(buffer, mock_sb, size);
            return size;
        }
        memset(buffer, 0, size);
        return size;
    }
    return 0;
}

size_t write_fs(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    return 0;
}

// Include implementation
#include "../../sys/fs/minix/minix.c"

// Test Logic
extern int printf(const char *format, ...); // From libc
extern void exit(int);

int main() {
    // Setup Mock Superblock (V1)
    struct minix_superblock *sb = (struct minix_superblock *)mock_sb;
    sb->s_magic = MINIX_V1_Magic;
    sb->s_ninodes = 100;
    sb->s_nzones = 100;
    sb->s_imap_blocks = 1;
    sb->s_zmap_blocks = 1;

    // Setup Mock Device
    fs_node_t dev;
    memset(&dev, 0, sizeof(dev));
    dev.flags = FS_BLOCKDEVICE;
    mock_dev_node = &dev;

    // Test Mount
    allocation_count = 0;
    free_count = 0;

    fs_node_t *root = minix_mount(NULL, 0, &dev);

    if (!root) {
        printf("FAIL: Mount returned NULL\n");
        exit(1);
    }

    if (root->unmount == NULL) {
        printf("FAIL: unmount callback not set\n");
        exit(1);
    }

    // Check if root->impl fits in 32-bit (non-zero)
    if (root->impl == 0) {
         printf("WARN: root->impl is 0 (NULL). Allocation failed or truncated?\n");
    } else {
         // Verify truncation didn't damage it
         minix_fs_t *fs = (minix_fs_t *)(uintptr_t)root->impl;
         // Check if fs is readable (magic check again)
         if (fs->sb.s_magic != MINIX_V1_Magic) {
             printf("FAIL: fs pointer corrupted (Magic mismatch)\n");
             exit(1);
         }
    }

    // Verify allocations happened
    printf("Allocations after mount: %d\n", allocation_count);
    if (allocation_count < 3) {
         printf("WARN: Unexpected allocation count %d (expected >= 3)\n", allocation_count);
    }

    // Test Unmount
    int result = root->unmount(root);
    if (result != 0) {
        printf("FAIL: unmount returned %d\n", result);
        exit(1);
    }

    // Verify Free
    printf("Frees after unmount: %d\n", free_count);

    // We expect free_count == allocation_count
    if (free_count != allocation_count) {
        printf("FAIL: Memory leak detected. Alloc: %d, Free: %d\n", allocation_count, free_count);
        exit(1);
    }

    printf("PASS: Minix unmount test passed\n");
    return 0;
}
