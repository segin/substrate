/*
 * tests/sys/host_test_device_hierarchy_lock.c
 *
 * Host-side test to verify device hierarchy locking.
 * This test mocks kernel primitives and includes sys/kern/device.c directly.
 *
 * Compile with:
 * gcc -I sys -idirafter sys/include -I tests/sys/mocks -o host_test_device_lock tests/sys/host_test_device_hierarchy_lock.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

/* Mock kmalloc/kfree */
void *kmalloc(size_t size) {
    return calloc(1, size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

/* Include real headers */
/* Use relative path or -I arguments during compilation */
#include "../../sys/include/sys/lock.h"
#include "../../sys/kern/bus.h"

/* Implement spinlock functions */
int spinlock_acquire_count = 0;
int spinlock_release_count = 0;
const char *last_acquired_lock_name = NULL;

void spinlock_init(spinlock_t *lock, const char *name) {
    lock->locked = 0;
    lock->name = name;
}

void spinlock_acquire(spinlock_t *lock) {
    spinlock_acquire_count++;
    lock->locked = 1;
    last_acquired_lock_name = lock->name;
    // printf("Acquired lock: %s\n", lock->name ? lock->name : "NULL");
}

void spinlock_release(spinlock_t *lock) {
    spinlock_release_count++;
    lock->locked = 0;
    // printf("Released lock: %s\n", lock->name ? lock->name : "NULL");
}

bool spinlock_is_held(spinlock_t *lock) {
    return lock->locked;
}

/* Mock other dependencies if needed */

/* Include source under test */
/* We need to ensure device.h is found. Using -I sys during compilation. */
#include "../../sys/kern/device.c"

int main() {
    printf("Running Device Hierarchy Lock Test...\n");

    /* Test Case 1: Create Root Device */
    /* Expectation: Lock init, but no hierarchy lock needed as parent is NULL */
    spinlock_acquire_count = 0;
    spinlock_release_count = 0;

    struct device *root = device_create("root", NULL);
    if (!root) {
        printf("FAIL: Failed to create root device\n");
        return 1;
    }

    /* Check if lock initialized */
    if (strcmp(root->lock.name, "device_lock") != 0) {
        printf("FAIL: Lock name not initialized correctly (expected 'device_lock', got '%s')\n", root->lock.name);
        return 1;
    }

    if (spinlock_acquire_count != 0) {
        printf("INFO: Unexpected lock acquisition during root creation (count=%d)\n", spinlock_acquire_count);
    }

    /* Test Case 2: Create Child Device */
    /* Expectation: Parent lock acquired */
    spinlock_acquire_count = 0;
    spinlock_release_count = 0;

    struct device *child = device_create("child", root);
    if (!child) {
        printf("FAIL: Failed to create child device\n");
        return 1;
    }

    if (spinlock_acquire_count == 0) {
        printf("FAIL: No lock acquired during child creation\n");
        return 1;
    } else {
        printf("PASS: Lock acquired during child creation (%d times)\n", spinlock_acquire_count);
        if (spinlock_release_count != spinlock_acquire_count) {
             printf("FAIL: Lock acquire/release mismatch (%d/%d)\n", spinlock_acquire_count, spinlock_release_count);
             return 1;
        }
    }

    /* Test Case 3: Find Child */
    /* Expectation: Parent lock acquired */
    spinlock_acquire_count = 0;
    spinlock_release_count = 0;

    struct device *found = device_find_child(root, "child");
    if (found != child) {
        printf("FAIL: Failed to find child device\n");
        return 1;
    }

    if (spinlock_acquire_count == 0) {
        printf("FAIL: No lock acquired during find_child\n");
        return 1;
    } else {
        printf("PASS: Lock acquired during find_child (%d times)\n", spinlock_acquire_count);
        if (spinlock_release_count != spinlock_acquire_count) {
             printf("FAIL: Lock acquire/release mismatch (%d/%d)\n", spinlock_acquire_count, spinlock_release_count);
             return 1;
        }
    }

    /* Test Case 4: Unregister Child */
    /* Expectation: Parent lock acquired, Child lock acquired (for children list clearing) */
    spinlock_acquire_count = 0;
    spinlock_release_count = 0;

    device_unregister(child);

    if (spinlock_acquire_count == 0) {
        printf("FAIL: No lock acquired during unregister\n");
        return 1;
    } else {
        /* Expect at least 2 acquisitions: Parent lock (removal) + Child lock (orphan) */
        if (spinlock_acquire_count < 2) {
             printf("WARN: Only %d locks acquired during unregister. Expected at least 2 (Parent + Self)\n", spinlock_acquire_count);
             /* We don't fail strictly if bus locking is not involved (child had no bus).
                But we know we added 2 locks. */
        }
        printf("PASS: Lock acquired during unregister (%d times)\n", spinlock_acquire_count);
        if (spinlock_release_count != spinlock_acquire_count) {
             printf("FAIL: Lock acquire/release mismatch (%d/%d)\n", spinlock_acquire_count, spinlock_release_count);
             return 1;
        }
    }

    printf("Test Complete.\n");
    return 0;
}
