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
        // TTAS: Spin on read until lock appears free
        while (__atomic_load_n(&lock->locked, __ATOMIC_RELAXED)) {
#if defined(__i386__) || defined(__x86_64__)
            __asm__ volatile("pause");
#endif
        }
    }

    /* Set cpu_id atomically after acquisition (finding #27) */
    __atomic_store_n(&lock->cpu_id, id, __ATOMIC_RELEASE);
}

bool spinlock_try_acquire(spinlock_t *lock) {
    uint32_t id = lapic_get_id();

    if (spinlock_is_held(lock)) {
        return false;
    }

    /* Try once to acquire the lock */
    if (__atomic_exchange_n(&lock->locked, 1, __ATOMIC_ACQUIRE) == 0) {
        __atomic_store_n(&lock->cpu_id, id, __ATOMIC_RELEASE);
        return true;
    }

    return false;
}

void spinlock_release(spinlock_t *lock) {
    if (!spinlock_is_held(lock)) {
        panic("Error: Releasing spinlock not held by current CPU");
    }

    __atomic_store_n(&lock->cpu_id, 0xFFFFFFFF, __ATOMIC_RELAXED);
    
    // Atomic release
    __atomic_store_n(&lock->locked, 0, __ATOMIC_RELEASE);
}

bool spinlock_is_held(spinlock_t *lock) {
    uint32_t locked_before;
    uint32_t owner_cpu;
    uint32_t locked_after;

    /* Read a stable snapshot to avoid mixed-state observations. */
    do {
        locked_before = __atomic_load_n(&lock->locked, __ATOMIC_ACQUIRE);
        owner_cpu = __atomic_load_n(&lock->cpu_id, __ATOMIC_ACQUIRE);
        locked_after = __atomic_load_n(&lock->locked, __ATOMIC_ACQUIRE);
    } while (locked_before != locked_after);

    return (locked_before != 0) && (owner_cpu == lapic_get_id());
}
