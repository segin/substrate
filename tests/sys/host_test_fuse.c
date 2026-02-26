#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>

// Mock kernel types and headers
// We rely on -I flags to find sys/types.h and sys/vfs/vfs.h

// Forward declarations
struct fs_node;
typedef struct fs_node fs_node_t;
struct filesystem;
typedef struct filesystem filesystem_t;

// Mocks
void sched_sleep(void *chan);
void vfs_register_filesystem(filesystem_t *fs);
void devfs_register_device(fs_node_t *node);

// Globals for mocks
int sched_sleep_calls = 0;
int vfs_register_calls = 0;
int devfs_register_calls = 0;

void *sched_sleep_chan = NULL;

// Mock implementations
void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
    vfs_register_calls++;
}

void devfs_register_device(fs_node_t *node) {
    (void)node;
    devfs_register_calls++;
}

// Include source file
#include "../../sys/fs/fuse.c"

// Implementation of sched_sleep that interacts with fuse internals
void sched_sleep(void *chan) {
    sched_sleep_calls++;
    sched_sleep_chan = chan;

    // Simulate wakeup logic
    if (chan == &request_queue) {
        // If we are sleeping on request_queue, it means it was empty.
        // Let's populate it to simulate a request arrival.
        // This simulates: "I went to sleep, and woke up because an item arrived".

        if (fuse_q_head == fuse_q_tail) {
             struct fuse_in_header *req = &request_queue[fuse_q_head];
             req->len = sizeof(struct fuse_in_header);
             req->opcode = 123; // Test opcode
             req->unique = 456;
             fuse_q_head = (fuse_q_head + 1) % FUSE_QUEUE_SIZE;
        }
    }
}

// Tests

void test_fuse_init() {
    printf("Test: fuse_init\n");
    fuse_init();
    assert(devfs_register_calls == 1);
    assert(strcmp(fuse_device_node.name, "fuse") == 0);
    assert(fuse_device_node.flags == FS_CHARDEVICE);
    printf("PASS\n");
}

void test_fuse_fs_init() {
    printf("Test: fuse_fs_init\n");
    fuse_fs_init();
    assert(vfs_register_calls == 1);
    printf("PASS\n");
}

void test_fuse_mount() {
    printf("Test: fuse_mount\n");
    fs_node_t *root = fuse_mount("fuse", 0, NULL);
    assert(root != NULL);
    assert(root->flags == FS_DIRECTORY);
    assert(root->read != NULL);
    printf("PASS\n");
}

void test_fuse_dev_read_blocking() {
    printf("Test: fuse_dev_read (blocking)\n");

    // Reset state
    fuse_q_head = 0;
    fuse_q_tail = 0;
    sched_sleep_calls = 0;

    // Buffer for read
    uint8_t buffer[sizeof(struct fuse_in_header)];

    // Call read. Queue is empty. Should call sched_sleep, which populates queue.
    size_t bytes = fuse_dev_read(NULL, 0, sizeof(buffer), buffer);

    assert(sched_sleep_calls == 1);
    assert(bytes == sizeof(struct fuse_in_header));

    // Verify content matches what sched_sleep injected
    struct fuse_in_header *req = (struct fuse_in_header *)buffer;
    assert(req->opcode == 123);
    assert(req->unique == 456);

    printf("PASS\n");
}

void test_fuse_dev_read_immediate() {
    printf("Test: fuse_dev_read (immediate)\n");

    // Reset state
    fuse_q_head = 0;
    fuse_q_tail = 0;
    sched_sleep_calls = 0;

    // Pre-populate queue
    struct fuse_in_header *req = &request_queue[fuse_q_head];
    req->len = sizeof(struct fuse_in_header);
    req->opcode = 789;
    req->unique = 101112;
    fuse_q_head = (fuse_q_head + 1) % FUSE_QUEUE_SIZE;

    // Buffer for read
    uint8_t buffer[sizeof(struct fuse_in_header)];

    // Call read. Queue is not empty. Should NOT call sched_sleep.
    size_t bytes = fuse_dev_read(NULL, 0, sizeof(buffer), buffer);

    assert(sched_sleep_calls == 0);
    assert(bytes == sizeof(struct fuse_in_header));

    // Verify content
    struct fuse_in_header *read_req = (struct fuse_in_header *)buffer;
    assert(read_req->opcode == 789);
    assert(read_req->unique == 101112);

    printf("PASS\n");
}

void test_fuse_dev_read_buffer_too_small() {
    printf("Test: fuse_dev_read (buffer too small)\n");

    // Reset state
    fuse_q_head = 0;
    fuse_q_tail = 0;

    // Pre-populate queue
    fuse_q_head = (fuse_q_head + 1) % FUSE_QUEUE_SIZE;

    uint8_t buffer[1]; // Too small
    size_t bytes = fuse_dev_read(NULL, 0, sizeof(buffer), buffer);

    assert(bytes == 0);

    printf("PASS\n");
}

int main() {
    printf("Running host_test_fuse...\n");
    test_fuse_init();
    test_fuse_fs_init();
    test_fuse_mount();
    test_fuse_dev_read_blocking();
    test_fuse_dev_read_immediate();
    test_fuse_dev_read_buffer_too_small();
    printf("All tests passed!\n");
    return 0;
}
