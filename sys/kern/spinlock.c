#include <sys/lock.h>
#include <kern/panic.h>

// Forward declaration for CPU ID helper (architecture specific)
extern uint32_t lapic_get_id(void);

void spinlock_init(spinlock_t *lock, const char *name) {
    lock->locked = 0;
    lock->cpu_id = 0xFFFFFFFF;
    lock->name = name;
    lock->last_acquire_eip = 0;
}

void spinlock_acquire(spinlock_t *lock) {
    uint32_t id = lapic_get_id();

    if (spinlock_is_held(lock)) {
        /* Recursive acquire on the same CPU — we never release a
         * spinlock implicitly, so this means either (a) we acquired
         * it earlier in this call chain and forgot to release, or
         * (b) an interrupt handler took it without restoring before
         * returning.  Print the EIP of the *original* acquire so
         * scripts/resolve-trap.sh (or a manual addr2line) maps to
         * the exact line that left it held.  Lock name + that EIP
         * are usually enough to pin down the bug without a repro. */
        extern void kprintf(const char *, ...);
        kprintf("Deadlock: spinlock '%s' already held by CPU %u "
                "(acquired at eip=0x%08x)\n",
                lock->name ? lock->name : "<unnamed>",
                (unsigned)id,
                (unsigned)lock->last_acquire_eip);
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

    /* Set cpu_id and acquire-site atomically after acquisition. */
    __atomic_store_n(&lock->cpu_id, id, __ATOMIC_RELEASE);
    lock->last_acquire_eip = (uintptr_t)__builtin_return_address(0);
}

bool spinlock_try_acquire(spinlock_t *lock) {
    uint32_t id = lapic_get_id();

    if (spinlock_is_held(lock)) {
        return false;
    }

    /* Try once to acquire the lock */
    if (__atomic_exchange_n(&lock->locked, 1, __ATOMIC_ACQUIRE) == 0) {
        __atomic_store_n(&lock->cpu_id, id, __ATOMIC_RELEASE);
        lock->last_acquire_eip = (uintptr_t)__builtin_return_address(0);
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
