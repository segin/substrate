#include "../sys/lock.h"
#include "panic.h"

// Forward declaration for CPU ID helper (architecture specific)
extern uint32_t lapic_get_id(void);

void spinlock_init(spinlock_t *lock, const char *name) {
    lock->locked = 0;
    lock->cpu_id = 0xFFFFFFFF;
    lock->name = name;
}

// Atomic exchange helper
static inline uint32_t xchg(volatile uint32_t *addr, uint32_t newval) {
    uint32_t result;
    __asm__ volatile("xchgl %0, %1"
                     : "+m" (*addr), "=a" (result)
                     : "1" (newval)
                     : "cc");
    return result;
}

void spinlock_acquire(spinlock_t *lock) {
    uint32_t id = lapic_get_id();

    if (spinlock_is_held(lock)) {
        panic("Deadlock: Spinlock already held by current CPU");
    }

    // Spin until acquired
    while (xchg(&lock->locked, 1) != 0) {
        // Pause instruction for power saving during spin
        __asm__ volatile("pause");
    }

    lock->cpu_id = id;
}

void spinlock_release(spinlock_t *lock) {
    if (!spinlock_is_held(lock)) {
        panic("Error: Releasing spinlock not held by current CPU");
    }

    lock->cpu_id = 0xFFFFFFFF;
    
    // Atomic release
    __asm__ volatile("movl $0, %0" : "+m" (lock->locked) : : "memory");
}

bool spinlock_is_held(spinlock_t *lock) {
    return (lock->locked && lock->cpu_id == lapic_get_id());
}
