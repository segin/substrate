#include <sys/lock.h>
#include <sys/proc.h>
#include <kern/sched.h>
#include <kern/sleepq.h>
#include <kern/panic.h>
#include <stddef.h>

void mutex_init(mutex_t *m, const char *name) {
    m->locked = 0;
    m->owner = NULL;
    m->name = name;
    spinlock_init(&m->guard, "mutex_guard");
}

bool mutex_trylock(mutex_t *m) {
    if (__sync_bool_compare_and_swap(&m->locked, 0, 1)) {
        m->owner = current_thread;
        return true;
    }
    return false;
}

void mutex_lock(mutex_t *m) {
    thread_t *me = current_thread;

    if (m->locked && m->owner == me) {
        panic("Deadlock: recursive mutex_lock attempted");
    }

    // Fast path: Uncontended optimization
    // Try to grab lock without heavy spinlock first
    if (mutex_trylock(m)) {
        return;
    }

    // Adaptive Spin: Spin if the owner is currently running on another CPU.
    // This avoids expensive context switches for short-held locks.
    for (int i = 0; i < 1000; i++) {
        if (!m->locked) {
            if (mutex_trylock(m)) return;
        }

        // Defensive check: only dereference owner if lock is still held
        // and owner is not NULL.
        thread_t *owner = (thread_t *)m->owner;
        if (owner) {
            // Verify owner is still the owner and lock is held before dereferencing state
            if (m->locked && m->owner == owner) {
                if (owner->state != THREAD_RUNNING) {
                    break;
                }
            } else {
                // Lock was released or owner changed
                if (mutex_trylock(m)) return;
                break;
            }
        }

#if defined(__i386__) || defined(__x86_64__)
        __asm__ volatile("pause");
#endif
    }

    // Slow path
    spinlock_acquire(&m->guard);
    
    // Check again under guard
    while (__sync_lock_test_and_set(&m->locked, 1) != 0) {
        if (m->owner == me) {
            // Recursive lock attempt?
            // For now, infinite loop/panic or just allow (recursive mutex?)
            // Standard mutex is non-recursive. Panic or warn?
            // "deadlock"
        }
        
        sleepq_add(m, me);
        spinlock_release(&m->guard);
        sched_yield(); // Context switch
        
        // Creating logic to re-acquire guard
        spinlock_acquire(&m->guard);
    }
    
    m->owner = me;
    spinlock_release(&m->guard);
}

void mutex_unlock(mutex_t *m) {
    spinlock_acquire(&m->guard);

    if (!m->locked) {
        spinlock_release(&m->guard);
        panic("Error: unlocking unlocked mutex");
    }
    if (m->owner != current_thread) {
        spinlock_release(&m->guard);
        panic("Error: mutex unlock by non-owner");
    }

    m->owner = NULL;
    __sync_lock_release(&m->locked);
    
    // Wake one waiter (mutex is exclusive)
    if (sleepq_wake_one(m)) {
        // We woke someone, they will content for lock
    }
    
    spinlock_release(&m->guard);
}

bool mutex_is_held(mutex_t *m) {
    return m->locked && m->owner == current_thread;
}
