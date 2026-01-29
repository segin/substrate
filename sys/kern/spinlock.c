#include <sys/lock.h>
#include <kern/panic.h>

// Forward declaration for CPU ID helper (architecture specific)
extern uint32_t lapic_get_id(void);

void spinlock_init(spinlock_t *lock, const char *name) {
    lock->locked = 0;
    lock->cpu_id = 0xFFFFFFFF;
    lock->name = name;
}

void spinlock_acquire(spinlock_t *lock) {
    uint32_t id = lapic_get_id();

    if (spinlock_is_held(lock)) {
        panic("Deadlock: Spinlock already held by current CPU");
    }

    // Spin until acquired
    while (__atomic_exchange_n(&lock->locked, 1, __ATOMIC_ACQUIRE)) {
#if defined(__i386__) || defined(__x86_64__)
        // Pause instruction for power saving during spin
        __asm__ volatile("pause");
#endif
    }

    lock->cpu_id = id;
}

void spinlock_release(spinlock_t *lock) {
    if (!spinlock_is_held(lock)) {
        panic("Error: Releasing spinlock not held by current CPU");
    }

    lock->cpu_id = 0xFFFFFFFF;
    
    // Atomic release
    __atomic_store_n(&lock->locked, 0, __ATOMIC_RELEASE);
}

bool spinlock_is_held(spinlock_t *lock) {
    return (lock->locked && lock->cpu_id == lapic_get_id());
}
