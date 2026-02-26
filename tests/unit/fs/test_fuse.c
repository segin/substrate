#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>

// Include vfs/vfs.h to get fs_node_t definition
#include <vfs/vfs.h>

// Mock dependencies
void devfs_register_device(fs_node_t *node) { (void)node; }

// Rename conflicting functions
#define fuse_init real_fuse_init
#define fuse_fs_init real_fuse_fs_init

// Mock sched_sleep
static int sched_sleep_count = 0;
void mock_sched_sleep(void *chan);
#define sched_sleep mock_sched_sleep

// Include source
#include "../../../sys/fs/fuse.c"

// Implementation of mock_sched_sleep
void mock_sched_sleep(void *chan) {
    (void)chan;
    sched_sleep_count++;

    // Simulate data arrival to break the loop
    if (fuse_q_head == fuse_q_tail) {
        // Inject a dummy request
        struct fuse_in_header *req = &request_queue[fuse_q_head];
        req->len = sizeof(struct fuse_in_header);
        req->opcode = FUSE_INIT;
        req->unique = 12345;
        req->nodeid = 1;
        fuse_q_head = (fuse_q_head + 1) % FUSE_QUEUE_SIZE;
    }
}

// Test cases
bool test_fuse_read_blocking(void) {
    printf("Testing fuse_dev_read blocking behavior...\n");

    // Setup
    fuse_q_head = 0;
    fuse_q_tail = 0;
    sched_sleep_count = 0;

    struct fuse_in_header buffer;
    memset(&buffer, 0, sizeof(buffer));

    // Call read. It should block (call mock_sched_sleep), which injects data, then return.
    size_t bytes_read = fuse_dev_read(NULL, 0, sizeof(buffer), (uint8_t*)&buffer);

    if (bytes_read != sizeof(struct fuse_in_header)) {
        printf("FAIL: Expected %zu bytes, got %zu\n", sizeof(struct fuse_in_header), bytes_read);
        return false;
    }

    if (sched_sleep_count != 1) {
        printf("FAIL: Expected 1 sleep, got %d\n", sched_sleep_count);
        return false;
    }

    if (buffer.opcode != FUSE_INIT) {
        printf("FAIL: Incorrect data read. Opcode %d\n", buffer.opcode);
        return false;
    }

    if (buffer.unique != 12345) {
        printf("FAIL: Incorrect data read. Unique %llu\n", (unsigned long long)buffer.unique);
        return false;
    }

    return true;
}

bool test_fuse_read_nonblocking(void) {
    printf("Testing fuse_dev_read non-blocking behavior...\n");

    // Setup: Data already available
    fuse_q_head = 0;
    fuse_q_tail = 0;
    sched_sleep_count = 0;

    struct fuse_in_header *req = &request_queue[fuse_q_head];
    req->len = sizeof(struct fuse_in_header);
    req->opcode = FUSE_READ;
    req->unique = 67890;
    fuse_q_head = (fuse_q_head + 1) % FUSE_QUEUE_SIZE;

    struct fuse_in_header buffer;
    memset(&buffer, 0, sizeof(buffer));

    size_t bytes_read = fuse_dev_read(NULL, 0, sizeof(buffer), (uint8_t*)&buffer);

    if (bytes_read != sizeof(struct fuse_in_header)) {
        printf("FAIL: Expected %zu bytes, got %zu\n", sizeof(struct fuse_in_header), bytes_read);
        return false;
    }

    if (sched_sleep_count != 0) {
        printf("FAIL: Expected 0 sleeps, got %d\n", sched_sleep_count);
        return false;
    }

    if (buffer.opcode != FUSE_READ) {
        printf("FAIL: Incorrect data read. Opcode %d\n", buffer.opcode);
        return false;
    }

    if (buffer.unique != 67890) {
        printf("FAIL: Incorrect data read. Unique %llu\n", (unsigned long long)buffer.unique);
        return false;
    }

    return true;
}

bool test_fuse_read_buffer_too_small(void) {
    printf("Testing fuse_dev_read buffer too small...\n");

    size_t bytes_read = fuse_dev_read(NULL, 0, 1, NULL);
    if (bytes_read != 0) {
        printf("FAIL: Expected 0 bytes for small buffer, got %zu\n", bytes_read);
        return false;
    }
    return true;
}

bool test_fuse_read_wrap(void) {
    printf("Testing fuse_dev_read wrapping behavior...\n");

    // Setup: fuse_q_tail at the end of the buffer
    fuse_q_tail = FUSE_QUEUE_SIZE - 1;
    // Set fuse_q_head to wrap around (0)
    fuse_q_head = 0;
    sched_sleep_count = 0;

    // Place data at the tail position
    struct fuse_in_header *req = &request_queue[fuse_q_tail];
    req->len = sizeof(struct fuse_in_header);
    req->opcode = FUSE_WRITE;
    req->unique = 99999;

    // Note: head is already at 0, meaning we have 1 item at index FUSE_QUEUE_SIZE - 1.

    struct fuse_in_header buffer;
    memset(&buffer, 0, sizeof(buffer));

    size_t bytes_read = fuse_dev_read(NULL, 0, sizeof(buffer), (uint8_t*)&buffer);

    if (bytes_read != sizeof(struct fuse_in_header)) {
        printf("FAIL: Expected %zu bytes, got %zu\n", sizeof(struct fuse_in_header), bytes_read);
        return false;
    }

    // Verify tail wrapped to 0
    if (fuse_q_tail != 0) {
        printf("FAIL: fuse_q_tail did not wrap. Expected 0, got %d\n", fuse_q_tail);
        return false;
    }

    if (buffer.opcode != FUSE_WRITE) {
        printf("FAIL: Incorrect data read. Opcode %d\n", buffer.opcode);
        return false;
    }

    return true;
}

bool test_fuse_read_multiple(void) {
    printf("Testing fuse_dev_read multiple items...\n");

    // Setup: 3 items
    fuse_q_head = 0;
    fuse_q_tail = 0;
    sched_sleep_count = 0;

    for (int i = 0; i < 3; i++) {
        struct fuse_in_header *req = &request_queue[fuse_q_head];
        req->len = sizeof(struct fuse_in_header);
        req->opcode = FUSE_FSYNC;
        req->unique = 100 + i;
        fuse_q_head = (fuse_q_head + 1) % FUSE_QUEUE_SIZE;
    }

    struct fuse_in_header buffer;

    // Read 3 times
    for (int i = 0; i < 3; i++) {
        memset(&buffer, 0, sizeof(buffer));
        size_t bytes_read = fuse_dev_read(NULL, 0, sizeof(buffer), (uint8_t*)&buffer);

        if (bytes_read != sizeof(struct fuse_in_header)) {
             printf("FAIL: Read %d: Expected %zu bytes, got %zu\n", i, sizeof(struct fuse_in_header), bytes_read);
             return false;
        }

        if (buffer.unique != (uint64_t)(100 + i)) {
             printf("FAIL: Read %d: Expected unique %d, got %llu\n", i, 100 + i, (unsigned long long)buffer.unique);
             return false;
        }
    }

    // Queue should be empty now
    if (fuse_q_head != fuse_q_tail) {
        printf("FAIL: Queue should be empty. Head %d, Tail %d\n", fuse_q_head, fuse_q_tail);
        return false;
    }

    return true;
}

bool test_fuse_read(void) {
    bool pass = true;
    pass &= test_fuse_read_blocking();
    pass &= test_fuse_read_nonblocking();
    pass &= test_fuse_read_buffer_too_small();
    pass &= test_fuse_read_wrap();
    pass &= test_fuse_read_multiple();
    return pass;
}
