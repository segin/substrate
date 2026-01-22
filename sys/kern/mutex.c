#include <sys/lock.h>
#include <sys/proc.h>
#include <kern/sched.h>
#include <stddef.h>

// Atomic exchange helper (from spinlock.c logic)
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

void mutex_init(mutex_t *m, const char *name) {
    m->locked = 0;
    m->owner = NULL;
    m->name = name;
}

void mutex_lock(mutex_t *m) {
    // If we can't acquire, sleep on the mutex address
    while (xchg(&m->locked, 1) != 0) {
        sched_sleep(m);
    }
    m->owner = current_thread;
}

void mutex_unlock(mutex_t *m) {
    m->owner = NULL;
#ifndef HOST_TEST
    __asm__ volatile("movl $0, %0" : "+m" (m->locked) : : "memory");
#else
    __sync_lock_release(&m->locked);
#endif
    // Wake up everyone waiting on this mutex
    sched_wakeup(m);
}

bool mutex_is_held(mutex_t *m) {
    return m->locked && m->owner == current_thread;
}
