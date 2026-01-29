#include <sys/lock.h>
#include <sys/proc.h>
#include <kern/sched.h>
#include <kern/sleepq.h>
#include <stddef.h>

void sema_init(semaphore_t *s, int value, const char *name) {
    s->value = value;
    spinlock_init(&s->lock, name);
    s->name = name;
}

void sema_wait(semaphore_t *s) {
    spinlock_acquire(&s->lock);
    while (s->value <= 0) {
        // Sleep on the semaphore address
        sleepq_add(s, current_thread);
        spinlock_release(&s->lock);
        sched_yield(); // Context switch (will sleep)
        
        // Re-acquire lock after waking
        spinlock_acquire(&s->lock);
    }
    s->value--;
    spinlock_release(&s->lock);
}

void sema_post(semaphore_t *s) {
    spinlock_acquire(&s->lock);
    s->value++;
    if (s->value > 0) {
        // Wake one waiter
        sleepq_wake_one(s);
    }
    spinlock_release(&s->lock);
}

int sema_getvalue(semaphore_t *s) {
    int val;
    spinlock_acquire(&s->lock);
    val = s->value;
    spinlock_release(&s->lock);
    return val;
}
