/*
 * pthread_atfork — register handlers run around fork(3).
 *
 * POSIX ordering: prepare handlers run in the reverse of registration order
 * (LIFO); parent and child handlers run in registration order (FIFO).  The
 * list is kept newest-first (each new registration is prepended), so walking
 * it head->tail is LIFO (prepare) and tail->head is FIFO (parent/child).
 *
 * libc's fork() calls the three __pthread_atfork_* hooks below through weak
 * references, so a program that never links libpthread pays nothing and a
 * program that does gets the handlers run automatically around every fork().
 *
 * Other pthread functions live in pthread_create.c, pthread_mutex.c,
 * pthread_cond.c, pthread_extra.c, pthread_barrier.c, pthread_spin.c.
 */
#include "pthread.h"
#include <errno.h>
#include <sched.h>
#include <stdlib.h>

struct atfork_handler {
    void (*prepare)(void);
    void (*parent)(void);
    void (*child)(void);
    struct atfork_handler *next;
};

static struct atfork_handler *atfork_list;   /* newest first */
static int atfork_lock;

static void af_lock(void)   { while (__sync_lock_test_and_set(&atfork_lock, 1)) sched_yield(); }
static void af_unlock(void) { __sync_lock_release(&atfork_lock); }

int pthread_atfork(void (*prepare)(void), void (*parent)(void),
                   void (*child)(void)) {
    struct atfork_handler *h = malloc(sizeof(*h));
    if (!h)
        return ENOMEM;
    h->prepare = prepare;
    h->parent  = parent;
    h->child   = child;

    af_lock();
    h->next = atfork_list;
    atfork_list = h;
    af_unlock();
    return 0;
}

/*
 * Run parent/child handlers oldest-first (FIFO).  The list is kept
 * newest-first, so we reverse it in place, walk it head->tail, then reverse it
 * back to restore the original order.  This is O(1) stack — the previous
 * implementation recursed once per handler, so a program that registers many
 * atfork handlers (OPTS pthread_atfork/3-2 registers 10000) overflowed the
 * caller's 64 KiB thread stack and crashed inside fork().  The caller holds
 * af_lock (parent) or is single-threaded (child), so the transient in-place
 * mutation is not observable by any other thread.
 */
static void run_fifo(struct atfork_handler *h, int child) {
    struct atfork_handler *prev = NULL, *cur = h, *next;

    /* Reverse newest-first -> oldest-first. */
    while (cur) {
        next = cur->next;
        cur->next = prev;
        prev = cur;
        cur = next;
    }
    /* Walk oldest-first, invoking each parent/child handler. */
    for (cur = prev; cur; cur = cur->next) {
        void (*fn)(void) = child ? cur->child : cur->parent;
        if (fn)
            fn();
    }
    /* Reverse back to restore the newest-first order for the next fork(). */
    cur = prev;
    prev = NULL;
    while (cur) {
        next = cur->next;
        cur->next = prev;
        prev = cur;
        cur = next;
    }
}

/* Called by libc fork() before forking — prepare handlers, LIFO. */
void __pthread_atfork_prepare(void) {
    af_lock();
    for (struct atfork_handler *h = atfork_list; h; h = h->next)
        if (h->prepare)
            h->prepare();
    af_unlock();
}

/* Called by libc fork() in the parent after forking — parent handlers, FIFO. */
void __pthread_atfork_parent(void) {
    af_lock();
    run_fifo(atfork_list, 0);
    af_unlock();
}

/* Called by libc fork() in the child after forking — child handlers, FIFO.
 * The child is single-threaded here, so the (possibly held) lock is dropped
 * unconditionally rather than acquired. */
void __pthread_atfork_child(void) {
    atfork_lock = 0;
    run_fifo(atfork_list, 1);
}
