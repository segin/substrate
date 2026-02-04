// Standalone test for Minix inode freeing logic
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

int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++; s2++; n--;
    }
    if (n == 0) return 0;
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

size_t strlen(const char *s) {
    size_t len = 0;
    while (*s++) len++;
    return len;
}

int sprintf(char *str, const char *format, ...) {
    (void)format;
    *str = '\0';
    return 0;
}

// Mock Kernel Functions
extern int printf(const char *format, ...);
void kprint(const char *s) {
    // printf("%s", s);
    (void)s;
}

// Memory tracking and 32-bit allocation
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20
#define MAP_32BIT 0x40
#define MAP_FAILED ((void*)-1)

extern void *mmap(void *addr, unsigned long length, int prot, int flags, int fd, long offset);
extern int munmap(void *addr, unsigned long length);

void *kmalloc(size_t size) {
    unsigned long len = size;
    if (len == 0) return NULL;
    if (len < 4096) len = 4096;
    void *ptr = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (ptr == MAP_FAILED) return NULL;
    memset(ptr, 0, size); // Minix code often assumes zero-initialized?
    return ptr;
}

void kfree(void *ptr, size_t size) {
    if (ptr) {
        unsigned long len = size;
        if (len < 4096) len = 4096;
        munmap(ptr, len);
    }
}

// VFS Mocks
#include <vfs/vfs.h>

void vfs_register_filesystem(filesystem_t *fs) { (void)fs; }

// Test Control Flags
static bool fail_write_fs_inode = false;
static bool fail_write_fs_dir = false;

// Mock Disk
static uint8_t mock_sb[1024];      // Block 1
static uint8_t mock_imap[1024];    // Block 2
static uint8_t mock_zmap[1024];    // Block 3
static uint8_t mock_inodes[1024];  // Block 4 (Inodes 1..32)
static uint8_t mock_data[1024];    // Block 5 (Root Dir Data)

fs_node_t *mock_dev_node;

size_t read_fs(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    if (node != mock_dev_node) return 0;

    // Simple block mapping
    uint32_t block = offset / 1024;
    uint32_t boffset = offset % 1024;
    if (boffset + size > 1024) return 0; // Don't handle cross-block reads here

    uint8_t *src = NULL;
    if (block == 1) src = mock_sb;
    else if (block == 2) src = mock_imap;
    else if (block == 3) src = mock_zmap;
    else if (block == 4) src = mock_inodes; // Inodes
    else if (block == 5) src = mock_data;   // Data (Root dir content)

    if (src) {
        memcpy(buffer, src + boffset, size);
        return size;
    }
    memset(buffer, 0, size);
    return size;
}

size_t write_fs(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    if (node != mock_dev_node) return 0;

    uint32_t block = offset / 1024;
    uint32_t boffset = offset % 1024;
    if (boffset + size > 1024) return 0;

    uint8_t *dst = NULL;
    if (block == 1) dst = mock_sb;
    else if (block == 2) dst = mock_imap;
    else if (block == 3) dst = mock_zmap;
    else if (block == 4) {
        if (fail_write_fs_inode) return 0; // Simulate failure
        dst = mock_inodes;
    }
    else if (block == 5) {
        if (fail_write_fs_dir) return 0; // Simulate failure
        dst = mock_data;
    }

    if (dst) {
        memcpy(dst + boffset, buffer, size);
        return size;
    }
    // Allow writes to other blocks (succeed but discard)
    return size;
}

// Include implementation
#include "../../sys/fs/minix/minix.c"

extern void exit(int);

int main() {
    printf("Starting Minix Inode Free Tests...\n");

    // 1. Setup Mock Device & FS
    fs_node_t dev;
    memset(&dev, 0, sizeof(dev));
    dev.flags = FS_BLOCKDEVICE;
    mock_dev_node = &dev;

    struct minix_superblock *sb = (struct minix_superblock *)mock_sb;
    sb->s_magic = MINIX_V1_Magic;
    sb->s_ninodes = 32;
    sb->s_nzones = 100;
    sb->s_imap_blocks = 1;
    sb->s_zmap_blocks = 1;
    sb->s_firstdatazone = 5; // Block 5 is data
    sb->s_log_zone_size = 0;

    // Init Inodes block (Root inode needs valid zone)
    struct minix_inode_v1 *root_inode = (struct minix_inode_v1 *)mock_inodes; // Inode 1 is at index 0 because logic: block + (1-1)/...
    root_inode->i_mode = 0x4000 | 0777; // Dir
    root_inode->i_size = 32; // Empty dir (just . and .. implied? or empty)
    root_inode->i_zone[0] = 5; // Point to data block 5

    // Mount to get root node
    fs_node_t *root = minix_mount(NULL, 0, &dev);
    if (!root) {
        printf("FAIL: Mount failed\n");
        exit(1);
    }

    // Ensure IMAP is clear (except maybe bit 0? No, Inode 1 is usually bit 0.
    // Logic: inode_num 1 -> bit 0 of block.
    // minix_alloc_inode: searches for bit 0.
    // If we want to allocate Inode 2, we should ensure Inode 1 is taken.
    mock_imap[0] |= 1; // Mark Inode 1 as taken

    // --- Test Case 1: Write Inode Failure ---
    printf("Test 1: Write Inode Failure... ");
    fail_write_fs_inode = true;
    fail_write_fs_dir = false;

    // Attempt mknod "test1"
    int res = minix_mknod(root, "test1", 0x8000 | 0644, 0);

    if (res != -1) {
        printf("FAIL: minix_mknod should have failed\n");
        exit(1);
    }

    // Verify Inode 2 is free in map
    // Inode 2 corresponds to bit 1 of byte 0
    if ((mock_imap[0] & 2) != 0) {
        printf("FAIL: Inode bit was not cleared after write failure\n");
        exit(1);
    }
    printf("PASS\n");

    // --- Test Case 2: Dir Add Failure ---
    printf("Test 2: Dir Add Failure... ");
    fail_write_fs_inode = false;
    fail_write_fs_dir = true;

    // Attempt mknod "test2"
    res = minix_mknod(root, "test2", 0x8000 | 0644, 0);

    if (res != -1) {
        printf("FAIL: minix_mknod should have failed\n");
        exit(1);
    }

    // Verify Inode 2 is free in map
    if ((mock_imap[0] & 2) != 0) {
        printf("FAIL: Inode bit was not cleared after dir add failure\n");
        exit(1);
    }
    printf("PASS\n");

    return 0;
}
