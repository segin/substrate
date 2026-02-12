// Reproduction test for Out-of-bounds Read in Minix Driver readdir
// Mocks necessary kernel environment and overrides read_fs

// Prevent inclusion of project headers that force 32-bit types
#define _SYS_TYPES_H
#define _STDDEF_H

// Mock headers matching host (64-bit)
#include <stdint.h>
#include <stdbool.h>

// Define NULL
#define NULL ((void*)0)

// Define types compatible with 64-bit host environment
typedef int32_t pid_t;
typedef int32_t tid_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef int64_t off_t;
typedef int64_t blkcnt_t;
typedef uint64_t ino_t;
typedef uint32_t nlink_t;
typedef uint32_t blksize_t;
typedef uint64_t size_t;
typedef int64_t ssize_t;
typedef int32_t mode_t;
typedef uint32_t dev_t;
typedef int64_t time_t;
typedef int64_t fpos_t;
typedef int64_t ptrdiff_t;

// Additional types needed by vfs.h
typedef uint32_t clock_t;
typedef int32_t  clockid_t;
typedef int32_t  timer_t;
typedef int64_t  useconds_t;
typedef int64_t  suseconds_t;
typedef uint32_t id_t;
typedef int32_t  key_t;
typedef uint64_t fsblkcnt_t;
typedef uint64_t fsfilcnt_t;

typedef int32_t  pthread_t;
typedef int32_t  pthread_attr_t;
typedef int32_t  pthread_mutex_t;
typedef int32_t  pthread_mutexattr_t;
typedef int32_t  pthread_cond_t;
typedef int32_t  pthread_condattr_t;
typedef int32_t  pthread_key_t;
typedef int32_t  pthread_once_t;
typedef int32_t  pthread_rwlock_t;
typedef int32_t  pthread_rwlockattr_t;
typedef int32_t  pthread_spinlock_t;
typedef int32_t  pthread_barrier_t;
typedef int32_t  pthread_barrierattr_t;

// Standard string functions
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

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++; s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    while (n-- > 0 && *s1 && *s2) {
        if (*s1 != *s2) return *(const unsigned char *)s1 - *(const unsigned char *)s2;
        s1++; s2++;
    }
    return 0;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
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
    (void)format;
    *str = '\0';
    return 0;
}

extern int printf(const char *format, ...);
extern void exit(int);

char *strcpy(char *dest, const char *src) {
    int i = 0;
    char *ret = dest;
    while (1) {
        if (i >= 30) {
            printf("VULNERABILITY TRIGGERED: strcpy read past 30 bytes without null terminator!\n");
            exit(1);
        }
        char c = *src++;
        *dest++ = c;
        if (c == 0) break;
        i++;
    }
    return ret;
}

// Mock Kernel Functions
int64_t get_time(void) {
    return 0;
}

void kprint(const char *s) {
    (void)s;
}

void *kmalloc(size_t size) {
    static char heap[1024 * 1024];
    static size_t heap_ptr = 0;
    void *ptr = &heap[heap_ptr];
    heap_ptr += (size + 7) & ~7;
    return ptr;
}

void kfree(void *ptr, size_t size) {
    (void)ptr; (void)size;
}

// VFS Mocks
#include <vfs/vfs.h>

void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

// read_fs mock
size_t read_fs(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    // Check if reading from our test zone (123)
    // Offset will be zone * 1024
    if (offset == 123 * 1024) {
        // We are reading the block containing directory entries.
        // We need to return a block where the first entry is our malicious one.
        memset(buffer, 0, size);

        // struct minix_dirent_v1 is defined in minix.h which is included by minix.c
        // We can't use it easily here unless we include minix.c or define it.
        // But minix.c is included below.
        // So we can use a helper or just byte manipulation.
        // Layout: inode (2 bytes), name (30 bytes).

        // Entry 0:
        buffer[0] = 0x39; // Inode 12345 (0x3039)
        buffer[1] = 0x30;

        // Name: 30 'A's
        for (int i = 0; i < 30; i++) {
            buffer[2 + i] = 'A';
        }

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

int main() {
    printf("Starting reproduction test...\n");

    // Setup Minix FS structure
    minix_fs_t fs;
    memset(&fs, 0, sizeof(fs));
    fs.sb.s_magic = MINIX_V1_Magic;
    fs.sb.s_firstdatazone = 100;
    fs.sb.s_log_zone_size = 0; // Zone size = Block size = 1024

    // Setup Inode (V1)
    struct minix_inode_v1 *inode = (struct minix_inode_v1 *)kmalloc(sizeof(struct minix_inode_v1));
    memset(inode, 0, sizeof(struct minix_inode_v1));
    inode->i_mode = 0x4000; // Directory
    inode->i_size = 32; // One entry
    inode->i_zone[0] = 123; // Direct zone 0 points to block 123

    // Setup FS Node
    fs_node_t node;
    memset(&node, 0, sizeof(node));
    node.impl = (uintptr_t)&fs;
    node.ptr = (struct fs_node *)inode; // minix.c casts node->ptr to struct minix_inode_v1*
    node.length = 32; // Length matches size

    printf("Calling minix_readdir...\n");

    // minix_readdir(node, 0)
    struct dirent *d = minix_readdir(&node, 0);

    if (d) {
        printf("readdir returned successfully. Name: %s\n", d->name);
        if (strlen(d->name) == 30 && d->name[30] == '\0') {
            printf("PASS: Name is correctly truncated and null-terminated.\n");
            return 0;
        } else {
             printf("FAIL: Name is invalid length: %zu\n", strlen(d->name));
             return 1;
        }
    } else {
        printf("FAIL: readdir returned NULL\n");
        return 1;
    }

    return 0;
}
