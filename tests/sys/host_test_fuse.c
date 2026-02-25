/*
 * host_test_fuse.c - Host-side unit tests for sys/fs/fuse.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

// Mocking kernel types and functions for host environment

// Include standard types first to define standard integer types
#include <sys/types.h>

// Mock sched_sleep
int sched_sleep_calls = 0;
void *sched_sleep_chan = NULL;

// Forward declaration of populate function
void populate_queue_in_sleep(void);

void sched_sleep(void *chan) {
    sched_sleep_calls++;
    sched_sleep_chan = chan;

    // Simulate wakeup logic
    populate_queue_in_sleep();
}

// Forward declarations for mocks
struct fs_node;
typedef struct fs_node fs_node_t;

struct filesystem;
typedef struct filesystem filesystem_t;

// Mocks for registration
filesystem_t *registered_fs = NULL;
void vfs_register_filesystem(filesystem_t *fs) {
    registered_fs = fs;
}

fs_node_t *registered_device_node = NULL;
void devfs_register_device(fs_node_t *node) {
    registered_device_node = node;
}

// Include the source file directly
// This allows access to static variables and internal functions
#include "../../sys/fs/fuse.c"

// Implementation of populate_queue_in_sleep needs access to static vars from fuse.c
void populate_queue_in_sleep(void) {
    // Only populate if sleeping on request_queue
    if (sched_sleep_chan == (void*)&request_queue) {
        // If queue is empty, add a dummy request
        if (fuse_q_head == fuse_q_tail) {
            struct fuse_in_header *req = &request_queue[fuse_q_head];
            req->len = sizeof(struct fuse_in_header);
            req->opcode = 0xDEADBEEF; // Marker
            req->unique = 12345;
            fuse_q_head = (fuse_q_head + 1) % FUSE_QUEUE_SIZE;
        }
    }
}

// Tests

void test_fuse_init(void) {
    printf("Test: fuse_init\n");
    registered_device_node = NULL;
    fuse_init();
    assert(registered_device_node != NULL);
    assert(strcmp(registered_device_node->name, "fuse") == 0);
    assert(registered_device_node->flags == FS_CHARDEVICE);
    printf("PASS\n");
}

void test_fuse_mount(void) {
    printf("Test: fuse_mount\n");
    // fuse_mount is static in fuse.c, but visible here.
    fs_node_t *root = fuse_mount("fuse", 0, NULL);
    assert(root != NULL);
    assert(root->flags == FS_DIRECTORY);
    // Check vtable
    assert(root->read == &fuse_vfs_read);
    printf("PASS\n");
}

void test_fuse_dev_read_blocking(void) {
    printf("Test: fuse_dev_read blocking behavior\n");

    // Setup
    fuse_q_head = 0;
    fuse_q_tail = 0;
    sched_sleep_calls = 0;
    sched_sleep_chan = NULL;

    uint8_t buffer[sizeof(struct fuse_in_header)];

    // Call read. It should sleep, then get populated, then return.
    size_t bytes = fuse_dev_read(NULL, 0, sizeof(buffer), buffer);

    assert(bytes == sizeof(struct fuse_in_header));
    assert(sched_sleep_calls == 1);
    assert(sched_sleep_chan == (void*)&request_queue);

    // Verify data read matches what we put in populate_queue_in_sleep
    struct fuse_in_header *req = (struct fuse_in_header *)buffer;
    assert(req->opcode == 0xDEADBEEF);
    assert(req->unique == 12345);

    printf("PASS\n");
}

void test_fuse_dev_read_immediate(void) {
    printf("Test: fuse_dev_read immediate behavior\n");

    // Setup
    fuse_q_head = 0;
    fuse_q_tail = 0;
    sched_sleep_calls = 0;

    // Pre-populate queue
    struct fuse_in_header *req = &request_queue[fuse_q_head];
    req->len = sizeof(struct fuse_in_header);
    req->opcode = 0xCAFEBABE;
    fuse_q_head = (fuse_q_head + 1) % FUSE_QUEUE_SIZE;

    uint8_t buffer[sizeof(struct fuse_in_header)];

    // Call read. It should NOT sleep.
    size_t bytes = fuse_dev_read(NULL, 0, sizeof(buffer), buffer);

    assert(bytes == sizeof(struct fuse_in_header));
    assert(sched_sleep_calls == 0);

    struct fuse_in_header *res = (struct fuse_in_header *)buffer;
    assert(res->opcode == 0xCAFEBABE);

    printf("PASS\n");
}

void test_fuse_dev_read_buffer_too_small(void) {
    printf("Test: fuse_dev_read buffer too small\n");

    uint8_t buffer[1];
    size_t bytes = fuse_dev_read(NULL, 0, sizeof(buffer), buffer);

    assert(bytes == 0);
    printf("PASS\n");
}

void test_fuse_dev_write(void) {
    printf("Test: fuse_dev_write stub\n");

    uint8_t buffer[sizeof(struct fuse_out_header)] = {0};
    // Just a stub for now, returns size.
    size_t bytes = fuse_dev_write(NULL, 0, sizeof(buffer), buffer);

    assert(bytes == sizeof(buffer));
    printf("PASS\n");
}

int main(void) {
    printf("Running host_test_fuse...\n");

    test_fuse_init();
    test_fuse_mount();
    test_fuse_dev_read_blocking();
    test_fuse_dev_read_immediate();
    test_fuse_dev_read_buffer_too_small();
    test_fuse_dev_write();

    printf("All tests passed!\n");
    return 0;
}
