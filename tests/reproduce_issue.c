#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/types.h>

// Mock kernel environment
typedef uint32_t kdev_t;

// Fix conflict with stdio.h vasprintf
#define vasprintf kernel_vasprintf

// Mock kprint
void kprint(const char *str) {
    // printf("%s", str);
}

int kprintf(const char *fmt, ...) {
    return 0;
}

// Mock kmalloc/kfree
void *kmalloc(size_t size) {
    return malloc(size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

int64_t get_time(void) {
    return time(NULL);
}

struct filesystem;
typedef struct filesystem filesystem_t;
void vfs_register_filesystem(filesystem_t *fs) { (void)fs; }

// Include the source
#include "../sys/fs/ext2/ext2.c"

// Helper to set up the FS
ext2_fs_t mock_fs;
fs_node_t mock_dev;

// Mock read/write with correct signatures (size_t)
size_t mock_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    // Simulate delay to trigger race
    usleep(50000); // 50ms

    // Fill buffer with a pattern derived from the block number
    uint32_t block_num = offset / 4096;

    // Pattern: Block Number (low byte)
    memset(buffer, block_num & 0xFF, size);

    return size;
}

size_t mock_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    return size;
}

void setup_mocks() {
    memset(&mock_fs, 0, sizeof(mock_fs));
    memset(&mock_dev, 0, sizeof(mock_dev));

    mock_dev.read = mock_read;
    mock_dev.write = mock_write;

    mock_fs.device = &mock_dev;
    mock_fs.block_size = 4096;
    mock_fs.inode_size = 128;
    mock_fs.inodes_per_group = 100;
    mock_fs.group_count = 1;

    mock_fs.bgd = malloc(sizeof(ext2_group_desc_t) * 1);
    mock_fs.bgd[0].bg_inode_table = 100; // Inode table at block 100
}

typedef struct {
    int thread_id;
    uint32_t inode_num;
    int success;
} thread_arg_t;

void *thread_func(void *arg) {
    thread_arg_t *args = (thread_arg_t *)arg;
    ext2_inode_t inode;

    // Thread 1 reads Inode 1 -> Block 100. Pattern 100 (0x64).
    // Thread 2 reads Inode 33 -> Block 101. Pattern 101 (0x65).

    ext2_read_inode(&mock_fs, args->inode_num, &inode);

    uint8_t *raw = (uint8_t *)&inode;
    uint8_t expected = (args->inode_num > 32 ? 101 : 100) & 0xFF;

    if (raw[0] != expected) {
        printf("Thread %d (Inode %d): FAIL. Expected 0x%02X, got 0x%02X\n",
               args->thread_id, args->inode_num, expected, raw[0]);
        args->success = 0;
    } else {
        args->success = 1;
    }

    return NULL;
}

int main() {
    setup_mocks();

    pthread_t t1, t2;
    thread_arg_t a1 = {1, 1, 0};   // Inode 1 (Block 100)
    thread_arg_t a2 = {2, 33, 0};  // Inode 33 (Block 101)

    printf("Starting concurrent read test (Race Condition Check)...\n");
    pthread_create(&t1, NULL, thread_func, &a1);
    pthread_create(&t2, NULL, thread_func, &a2);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    if (a1.success && a2.success) {
        printf("SUCCESS: No corruption detected.\n");
        return 0;
    } else {
        printf("FAILURE: Corruption detected! Static buffer race condition confirmed.\n");
        return 1;
    }
}
