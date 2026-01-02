#include <sys/lock.h>
#include <sys/proc.h>
#include "sched.h"
#include <stddef.h>

void sema_init(semaphore_t *s, int value, const char *name) {
    s->value = value;
    spinlock_init(&s->lock, name);
    s->name = name;
}

void sema_wait(semaphore_t *s) {
    spinlock_acquire(&s->lock);
    while (s->value <= 0) {
        // We must release the spinlock before sleeping to avoid deadlock,
        // but we need to ensure atomicity. 
        // In this simple prototype, sched_sleep will be called after release.
        spinlock_release(&s->lock);
        sched_sleep(s);
        spinlock_acquire(&s->lock);
    }
    s->value--;
    spinlock_release(&s->lock);
}

void sema_post(semaphore_t *s) {
    spinlock_acquire(&s->lock);
    s->value++;
    spinlock_release(&s->lock);
    sched_wakeup(s);
}

int sema_getvalue(semaphore_t *s) {
    int val;
    spinlock_acquire(&s->lock);
    val = s->value;
    spinlock_release(&s->lock);
    return val;
}
