#include <stddef.h>

#include <kern/console.h>
#include <kern/panic.h>
#include <kern/sched.h>
#include <kern/sleepq.h>
#include <sys/lock.h>
#include <sys/proc.h>

static void mutex_track_owner(mutex_t *m, thread_t *owner) {
    if (!m || !owner) {
        return;
    }
    m->owned_next = owner->held_mutexes;
    owner->held_mutexes = m;
}

static void mutex_untrack_owner(mutex_t *m, thread_t *owner) {
    mutex_t **pp;

    if (!m || !owner) {
        return;
    }

    pp = &owner->held_mutexes;
    while (*pp && *pp != m) {
        pp = &(*pp)->owned_next;
    }
    if (*pp == m) {
        *pp = m->owned_next;
    }
    m->owned_next = NULL;
}

void mutex_init(mutex_t *m, const char *name) {
    m->locked = 0;
    m->owner = NULL;
    m->name = name;
    m->owned_next = NULL;
    m->acq_pc = NULL;
    spinlock_init(&m->guard, "mutex_guard");
}

bool mutex_trylock(mutex_t *m) {
    if (__sync_bool_compare_and_swap(&m->locked, 0, 1)) {
        m->owner = current_thread;
        m->acq_pc = __builtin_return_address(0);
        mutex_track_owner(m, current_thread);
        return true;
    }
    return false;
}

void mutex_lock(mutex_t *m) {
    thread_t *me = current_thread;
    const void *pc = __builtin_return_address(0);

    if (m->locked && m->owner == me) {
        /*
         * Name both ends.  "recursive mutex_lock attempted" on its own says a
         * thread deadlocked against itself but not where, and the in-kernel
         * backtrace resolves static functions to the nearest preceding
         * exported symbol -- so the frame it blames is routinely the wrong
         * function.  Printing the lock plus the two call sites turns a
         * guessing exercise into two addresses to look up.
         */
        kprintf("VM/lock: mutex '%s' (%p) already held by thread %p\n"
                "  acquired from %p, re-entered from %p\n",
                m->name ? m->name : "?", (void *)m, (void *)me,
                m->acq_pc, pc);
        panic("Deadlock: recursive mutex_lock attempted");
    }

    // Fast path: Uncontended optimization
    // Try to grab lock without heavy spinlock first
    if (mutex_trylock(m)) {
        m->acq_pc = pc;
        return;
    }

    // Adaptive Spin: Spin if the owner is currently running on another CPU.
    // This avoids expensive context switches for short-held locks.
    for (int i = 0; i < 1000; i++) {
        if (!m->locked) {
            if (mutex_trylock(m)) { m->acq_pc = pc; return; }
        }

        // Defensive check: only dereference owner if lock is still held
        // and owner is not NULL.
        thread_t *owner = __atomic_load_n(&m->owner, __ATOMIC_ACQUIRE);
        if (owner) {
            // Verify owner is still the owner and lock is held
            if (!m->locked || __atomic_load_n(&m->owner, __ATOMIC_RELAXED) != owner) {
                // Lock was released or owner changed
                if (mutex_trylock(m)) { m->acq_pc = pc; return; }
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
    m->acq_pc = pc;
    mutex_track_owner(m, me);
    spinlock_release(&m->guard);
}

void mutex_unlock(mutex_t *m) {
    thread_t *owner;

    spinlock_acquire(&m->guard);

    if (!m->locked) {
        spinlock_release(&m->guard);
        panic("Error: unlocking unlocked mutex");
    }
    if (m->owner != current_thread) {
        spinlock_release(&m->guard);
        panic("Error: mutex unlock by non-owner");
    }

    owner = (thread_t *)m->owner;
    mutex_untrack_owner(m, owner);
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

int mutex_release_owned_by_thread(thread_t *owner) {
    int released = 0;

    while (owner && owner->held_mutexes) {
        mutex_t *m = owner->held_mutexes;

        spinlock_acquire(&m->guard);
        if (m->locked && m->owner == owner) {
            mutex_untrack_owner(m, owner);
            m->owner = NULL;
            __sync_lock_release(&m->locked);
            if (sleepq_wake_one(m)) {
                /* Waiter was made runnable. */
            }
            released++;
        } else {
            mutex_untrack_owner(m, owner);
        }
        spinlock_release(&m->guard);
    }

    return released;
}
