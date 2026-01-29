#include <sys/lock.h>
#include <sys/proc.h>
#include <kern/sched.h>
#include <kern/sleepq.h>
#include <stddef.h>

void mutex_init(mutex_t *m, const char *name) {
    m->locked = 0;
    m->owner = NULL;
    m->name = name;
    spinlock_init(&m->guard, "mutex_guard");
}

void mutex_lock(mutex_t *m) {
    thread_t *me = current_thread;

    // Fast path: Uncontended optimization
    // Try to grab lock without heavy spinlock first
    if (__sync_bool_compare_and_swap(&m->locked, 0, 1)) {
        m->owner = me;
        return;
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
    
    // Assert m->owner == current_thread
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
