#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

// Mock vfs types
// We need to ensure off_t and other types are defined.
// The host's sys/types.h should be sufficient if we use standard headers.
#include <sys/types.h>

// Mock vfs/vfs.h by including the real one (which includes sys/types.h)
// But we need to make sure vfs/vfs.h can be found.
// The Makefile will add -I../../sys

// Mock sched_sleep
void sched_sleep(void *channel);

// Mock devfs
struct fs_node;
typedef struct fs_node fs_node_t;
void devfs_register_device(fs_node_t *node);

// Mock vfs
struct filesystem;
typedef struct filesystem filesystem_t;
void vfs_register_filesystem(filesystem_t *fs);

// Include the source file
#include "../../sys/fs/fuse.c"

// Implementation of mocks

void sched_sleep(void *channel) {
    (void)channel;
    printf("sched_sleep called, simulating data arrival...\n");
    // Simulate data arriving
    // We access static request_queue and fuse_q_head from fuse.c
    struct fuse_in_header *req = &request_queue[fuse_q_head];
    req->len = sizeof(struct fuse_in_header);
    req->opcode = FUSE_READ;
    req->unique = 12345ULL;
    req->nodeid = 1;

    // Advance head to indicate data is available
    fuse_q_head = (fuse_q_head + 1) % FUSE_QUEUE_SIZE;
}

void devfs_register_device(fs_node_t *node) {
    (void)node;
}

void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

// Tests

void test_fuse_read_blocking() {
    printf("Running test_fuse_read_blocking...\n");

    // Reset queue
    fuse_q_head = 0;
    fuse_q_tail = 0;

    uint8_t buffer[1024];
    // fuse_dev_read will block because head == tail.
    // sched_sleep mock will be called, which will advance head.
    // Then loop check will fail (head != tail), and it will proceed.

    size_t bytes = fuse_dev_read(NULL, 0, sizeof(struct fuse_in_header), buffer);

    assert(bytes == sizeof(struct fuse_in_header));

    struct fuse_in_header *req = (struct fuse_in_header *)buffer;
    assert(req->opcode == FUSE_READ);
    assert(req->unique == 12345ULL);

    // Verify tail advanced
    assert(fuse_q_tail == 1);

    printf("Passed.\n");
}

void test_fuse_read_non_blocking() {
    printf("Running test_fuse_read_non_blocking...\n");

    // Reset queue
    fuse_q_head = 0;
    fuse_q_tail = 0;

    // Manually add data
    request_queue[fuse_q_head].opcode = FUSE_WRITE;
    fuse_q_head = (fuse_q_head + 1) % FUSE_QUEUE_SIZE;

    uint8_t buffer[1024];
    size_t bytes = fuse_dev_read(NULL, 0, sizeof(struct fuse_in_header), buffer);

    assert(bytes == sizeof(struct fuse_in_header));
    struct fuse_in_header *req = (struct fuse_in_header *)buffer;
    assert(req->opcode == FUSE_WRITE);

    printf("Passed.\n");
}

void test_fuse_read_small_buffer() {
    printf("Running test_fuse_read_small_buffer...\n");

    uint8_t buffer[1];
    size_t bytes = fuse_dev_read(NULL, 0, 1, buffer);
    assert(bytes == 0);

    printf("Passed.\n");
}

void test_circular_buffer_wrapping() {
    printf("Running test_circular_buffer_wrapping...\n");

    // Reset queue
    fuse_q_head = 0;
    fuse_q_tail = 0;

    // Fill up to the end
    fuse_q_head = FUSE_QUEUE_SIZE - 1;
    fuse_q_tail = FUSE_QUEUE_SIZE - 1;

    // Now sleep will be called, adding one item at FUSE_QUEUE_SIZE-1
    // And advancing head to 0.
    // Wait, my mock does (head + 1) % SIZE.
    // If head is SIZE-1, next is 0.

    uint8_t buffer[1024];
    size_t bytes = fuse_dev_read(NULL, 0, sizeof(struct fuse_in_header), buffer);

    assert(bytes == sizeof(struct fuse_in_header));
    assert(fuse_q_tail == 0); // Should wrap around to 0

    printf("Passed.\n");
}

int main() {
    test_fuse_read_blocking();
    test_fuse_read_non_blocking();
    test_fuse_read_small_buffer();
    test_circular_buffer_wrapping();
    printf("All tests passed!\n");
    return 0;
}
