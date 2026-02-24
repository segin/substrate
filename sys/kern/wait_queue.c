#include <sys/wait_queue.h>
#include <kern/sched.h>
#include <sys/lock.h>
#include <stddef.h>

void init_waitqueue_head(wait_queue_head_t *q) {
    spinlock_init(&q->lock, "wait_queue");
    TAILQ_INIT(&q->head);
}

void add_wait_queue(wait_queue_head_t *q, wait_queue_entry_t *wait) {
    spinlock_acquire(&q->lock);
    TAILQ_INSERT_TAIL(&q->head, wait, entry);
    spinlock_release(&q->lock);
}

void remove_wait_queue(wait_queue_head_t *q, wait_queue_entry_t *wait) {
    spinlock_acquire(&q->lock);
    TAILQ_REMOVE(&q->head, wait, entry);
    spinlock_release(&q->lock);
}

void wake_up_all(wait_queue_head_t *q) {
    wait_queue_entry_t *curr, *next;

    spinlock_acquire(&q->lock);
    TAILQ_FOREACH_SAFE(curr, &q->head, entry, next) {
        if (curr->func) {
            curr->func(curr, NULL);
        }
    }
    spinlock_release(&q->lock);
}

int default_wake_function(wait_queue_entry_t *wait, void *key) {
    (void)key;
    void *chan = wait->private;
    if (chan) {
        sched_wakeup(chan);
        return 1;
    }
    return 0;
}
