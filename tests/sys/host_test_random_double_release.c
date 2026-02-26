#define HOST_TEST 1
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/lock.h>

// Forward declarations for mocks
void kprint(const char *fmt, ...);
void sched_wakeup(void *chan);
void sched_sleep(void *chan);

typedef struct fs_node fs_node_t;
void devfs_register_device(fs_node_t *node);

// Include the source file directly to test static functions
#include <kern/random.c>

// Implement mocks
void spinlock_init(spinlock_t *lock, const char *name) {
    lock->locked = 0;
    lock->name = name;
}

int output_lock_release_count = 0;

void spinlock_acquire(spinlock_t *lock) {
    lock->locked = 1;
}

int spinlock_try_acquire(spinlock_t *lock) {
    if (lock->locked) return 0;
    lock->locked = 1;
    return 1;
}

void spinlock_release(spinlock_t *lock) {
    if (lock == &output_lock) {
        output_lock_release_count++;
        // Don't fail immediately, count them.
    }
    lock->locked = 0;
}

int spinlock_is_held(spinlock_t *lock) {
    return lock->locked;
}

void kprint(const char *fmt, ...) {}
void sched_wakeup(void *chan) {}
void sched_sleep(void *chan) {}
void devfs_register_device(fs_node_t *node) {}


int main() {
    printf("Running double release test...\n");

    // Initialize
    random_init();

    // Reset counters
    output_lock_release_count = 0;

    printf("Calling random_reseed...\n");
    random_reseed();

    printf("output_lock release count: %d\n", output_lock_release_count);

    if (output_lock_release_count == 1) {
        printf("Test Passed: output_lock released exactly once.\n");
        return 0;
    } else {
        printf("Test Failed: output_lock released %d times.\n", output_lock_release_count);
        return 1;
    }
}
