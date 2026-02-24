#include <kern/console.h>
#include <sys/lock.h>
#include <sys/types.h>
#include <string.h>
#include "tests.h"

static spinlock_t test_lock = SPINLOCK_INIT("test_lock");

void run_spinlock_tests(void) {
    kprint("Testing SPINLOCK_INIT...\n");

    // Test static initialization
    if (test_lock.locked != 0) {
        kprint("FAIL: test_lock.locked != 0\n");
        return;
    }

    if (test_lock.cpu_id != 0xFFFFFFFF) {
        kprint("FAIL: test_lock.cpu_id != 0xFFFFFFFF\n");
        return;
    }

    // Check name pointer (should be valid)
    if (!test_lock.name) {
        kprint("FAIL: test_lock.name is NULL\n");
        return;
    }

    if (strcmp(test_lock.name, "test_lock") != 0) {
        kprint("FAIL: test_lock.name mismatch\n");
        return;
    }

    // Acquire
    spinlock_acquire(&test_lock);

    if (!spinlock_is_held(&test_lock)) {
        kprint("FAIL: spinlock not held after acquire\n");
        return;
    }

    // Release
    spinlock_release(&test_lock);

    if (spinlock_is_held(&test_lock)) {
        kprint("FAIL: spinlock held after release\n");
        return;
    }

    kprint("SPINLOCK_INIT test passed\n");
}
