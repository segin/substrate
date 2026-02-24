#ifndef _SYS_WAIT_QUEUE_H
#define _SYS_WAIT_QUEUE_H

#include <sys/queue.h>
#include <sys/lock.h>

struct wait_queue_entry;

/*
 * Wait queue callback function.
 * Returns non-zero if the waiter was woken up (consumed), 0 otherwise.
 * For poll, we usually return 0 to keep waking others?
 * Actually, standard Linux wake_up returns void usually, or number of woken tasks.
 * The callback returns int (boolean).
 */
typedef int (*wait_queue_func_t)(struct wait_queue_entry *entry, void *key);

typedef struct wait_queue_entry {
    void *private;
    wait_queue_func_t func;
    TAILQ_ENTRY(wait_queue_entry) entry;
} wait_queue_entry_t;

typedef struct wait_queue_head {
    spinlock_t lock;
    TAILQ_HEAD(wait_queue_list, wait_queue_entry) head;
} wait_queue_head_t;

void init_waitqueue_head(wait_queue_head_t *q);
void add_wait_queue(wait_queue_head_t *q, wait_queue_entry_t *wait);
void remove_wait_queue(wait_queue_head_t *q, wait_queue_entry_t *wait);
void wake_up_all(wait_queue_head_t *q);

/* Default wakeup function that assumes 'private' is a thread_t* */
int default_wake_function(wait_queue_entry_t *wait, void *key);

#endif
