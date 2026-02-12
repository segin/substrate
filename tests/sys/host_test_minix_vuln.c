// Reproduction test for Minix readdir buffer overflow (CVE-Like)
// Simulates reading a non-null-terminated directory entry from disk

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/mman.h>

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

// Mock VFS types

struct fs_node {
    uint32_t inode;
    void *ptr;
};

typedef struct fs_node fs_node_t;

struct dirent {
    char name[256];
    uint32_t ino;
};

// Mock Minix structures
struct minix_dir_entry {
    uint16_t inode;
    char name[30];
};

// Functions to mock read_fs
size_t read_fs(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    if (size >= 32) {
        // Construct entry with 30 'A's and no null terminator
        uint16_t inode = 123;
        memcpy(buffer, &inode, 2);
        memset(buffer + 2, 'A', 30);
        
        // Fill the rest of the buffer with 'B's to detect over-read
        if (size > 32) {
            memset(buffer + 32, 'B', size - 32);
        }
    }
    return size;
}

// Poison stack helper
void poison_stack(void) {
    char buf[1024];
    memset(buf, 0xCC, sizeof(buf));
    (void)buf;
}

// The function under test (extracted from minix.c or mocked to match)
// In a real test, we would include minix.c, but for a simple host reproduction:
static struct dirent *minix_readdir_mock(fs_node_t *node, uint64_t index) {
    struct minix_dir_entry entry;
    // Simulate reading one entry
    read_fs(node, index * 32, 32, (uint8_t *)&entry);
    
    if (entry.inode == 0) return NULL;

    static struct dirent dir;
    // THE FIX:
    strncpy(dir.name, entry.name, 30);
    dir.name[30] = '\0';
    dir.ino = entry.inode;
    
    return &dir;
}

int main() {
    printf("Starting Minix readdir stack over-read reproduction test...\n");

    fs_node_t node;
    node.inode = 1;
    
    poison_stack();
    
    struct dirent *d = minix_readdir_mock(&node, 0);
    
    if (!d) {
        printf("FAIL: readdir returned NULL\n");
        return 1;
    }

    printf("Read entry name: '%s'\n", d->name);
    printf("Length: %zu\n", strlen(d->name));

    if (strlen(d->name) > 30) {
        printf("VULNERABLE: Buffer over-read detected! Length is %zu\n", strlen(d->name));
        return 1;
    } else {
        printf("SAFE: readdir handled non-null-terminated name correctly.\n");
    }

    return 0;
}
