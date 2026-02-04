#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <sys/types.h>

// Mock Implementations

void kprint(const char *str) {
    // printf("%s", str);
}

void *kmalloc(size_t size) {
    return malloc(size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

// Helper time mock
int64_t get_time(void) {
    return 0;
}

#include "mocks/vfs/vfs.h"

void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

// Include the source
#include "../sys/fs/ext2/ext2.c"

// --- TEST INFRASTRUCTURE ---

pthread_barrier_t barrier;

// Mock Device Read
// We use a global state to simulate "disk" content or logic.
static size_t mock_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;

    // Simulate inode table blocks.
    // We assume block size 1024.
    // We map Inode 1 to Block 100.
    // We map Inode 2 to Block 200.

    // Wait for all threads to reach this point
    pthread_barrier_wait(&barrier);

    // Add a tiny random delay to jitter the writes
    usleep(rand() % 100);

    uint32_t block_num = offset / 1024;

    if (block_num == 100) {
        // Fill with pattern for Inode 1
        memset(buffer, 0xAA, size);
    } else if (block_num == 200) {
        // Fill with pattern for Inode 2
        memset(buffer, 0xBB, size);
    } else {
        memset(buffer, 0, size);
    }

    return size;
}

static fs_node_t mock_device = {
    .read = mock_read
};

// Thread Worker
void *worker(void *arg) {
    int id = (int)(intptr_t)arg;

    // Setup a local fs struct
    ext2_fs_t fs;
    memset(&fs, 0, sizeof(fs));
    fs.device = &mock_device;
    fs.block_size = 1024;
    fs.inode_size = 128;
    fs.inodes_per_group = 10;
    fs.blocks_per_group = 1000;
    fs.group_count = 2;

    // Setup BGD
    // Since ext2_read_inode reads BGD from fs->bgd, we need to point it to something.
    ext2_group_desc_t bgd[2];
    memset(bgd, 0, sizeof(bgd));
    bgd[0].bg_inode_table = 100; // Inodes 1..10 -> Block 100
    bgd[1].bg_inode_table = 200; // Inodes 11..20 -> Block 200
    fs.bgd = bgd;

    // Inode to read
    uint32_t inode_num = (id == 1) ? 1 : 11; // 1 is in Group 0, 11 is in Group 1
    ext2_inode_t inode;

    int res = ext2_read_inode(&fs, inode_num, &inode);
    if (res != 0) {
        fprintf(stderr, "Thread %d: ext2_read_inode failed\n", id);
        return (void*)1;
    }

    // Verify content
    uint8_t expected = (id == 1) ? 0xAA : 0xBB;
    uint8_t *raw = (uint8_t*)&inode;

    // Check first few bytes
    if (raw[0] != expected) {
        // NOTE: In a race, we might get the other thread's data.
        fprintf(stderr, "Thread %d: Corruption! Expected 0x%02X, got 0x%02X\n", id, expected, raw[0]);
        return (void*)1;
    }

    return NULL;
}

int main() {
    pthread_t t1, t2;

    printf("Starting concurrency test (1000 iterations)...\n");

    pthread_barrier_init(&barrier, NULL, 2);

    for (int i = 0; i < 1000; i++) {
        pthread_create(&t1, NULL, worker, (void*)1);
        pthread_create(&t2, NULL, worker, (void*)2);

        void *r1, *r2;
        pthread_join(t1, &r1);
        pthread_join(t2, &r2);

        if (r1 || r2) {
            printf("Test FAILED: Race condition detected at iteration %d.\n", i);
            return 1;
        }
    }

    printf("Test PASSED: No corruption detected after 1000 iterations.\n");
    return 0;
}
