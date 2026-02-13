#include <sys/lock.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * Spinlock Unit Tests
 */

bool test_spinlock_basic(void) {
    spinlock_t lock;
    spinlock_init(&lock, "test-lock");
    
    if (lock.locked != 0) return false;
    
    spinlock_acquire(&lock);
    if (lock.locked != 1) return false;
    if (!spinlock_is_held(&lock)) return false;
    
    spinlock_release(&lock);
    if (lock.locked != 0) return false;
    if (spinlock_is_held(&lock)) return false;
    
    return true;
}

bool test_spinlock_initial_state(void) {
    spinlock_t lock;
    spinlock_init(&lock, "init-test");
    return (lock.locked == 0 && lock.cpu_id == 0xFFFFFFFF);
}
