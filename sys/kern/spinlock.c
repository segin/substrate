#include <sys/lock.h>
#include <kern/panic.h>

// Forward declaration for CPU ID helper (architecture specific)
extern uint32_t lapic_get_id(void);

void spinlock_init(spinlock_t *lock, const char *name) {
    lock->locked = 0;
    lock->cpu_id = 0xFFFFFFFF;
    lock->name = name;
}

// Atomic exchange helper
static inline uint32_t xchg(volatile uint32_t *addr, uint32_t newval) {
#ifndef HOST_TEST
    uint32_t result;
    __asm__ volatile("xchgl %0, %1"
                     : "+m" (*addr), "=a" (result)
                     : "1" (newval)
                     : "cc");
    return result;
#else
    return __sync_lock_test_and_set(addr, newval);
#endif
}

void spinlock_acquire(spinlock_t *lock) {
    uint32_t id = lapic_get_id();

    if (spinlock_is_held(lock)) {
        panic("Deadlock: Spinlock already held by current CPU");
    }

    // Spin until acquired
    while (xchg(&lock->locked, 1) != 0) {
        while (*(volatile uint32_t *)&lock->locked) {
#ifndef HOST_TEST
            // Pause instruction for power saving during spin
            __asm__ volatile("pause");
#endif
        }
    }

    lock->cpu_id = id;
}

void spinlock_release(spinlock_t *lock) {
    if (!spinlock_is_held(lock)) {
        panic("Error: Releasing spinlock not held by current CPU");
    }

    lock->cpu_id = 0xFFFFFFFF;
    
    // Atomic release
#ifndef HOST_TEST
    __asm__ volatile("movl $0, %0" : "+m" (lock->locked) : : "memory");
#else
    __sync_lock_release(&lock->locked);
#endif
}

bool spinlock_is_held(spinlock_t *lock) {
    return (lock->locked && lock->cpu_id == lapic_get_id());
}
