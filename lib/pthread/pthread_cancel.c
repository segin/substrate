/*
 * pthread_cancel.c — POSIX thread cancellation + the cleanup-handler stack.
 *
 * substrate previously stubbed cancellation as a no-op.  This provides a real
 * implementation that does NOT depend on signal delivery:
 *
 *   - A cancel is POSTED by setting a `cancel_pending` flag on the target
 *     thread's registry node (shared, lock-protected — see pthread_create.c).
 *     This works cross-thread without a signal, so it is unaffected by the
 *     kernel's lack of handler-running thread-directed signal delivery (a
 *     pthread_kill to a secondary thread sets the pending bit but does not run
 *     that thread's handler) and by the qemu/KVM post-signal coherence bug.
 *
 *   - Per-thread cancel state (ENABLE/DISABLE) and type (DEFERRED/ASYNC) and
 *     the cleanup stack live in TLS (initial-exec model — libpthread is a
 *     startup DT_NEEDED, never dlopen'd).
 *
 *   - A DEFERRED cancel is acted upon at the next cancellation point
 *     (pthread_testcancel, and the state/type setters when re-enabling or
 *     switching to async with a cancel already pending).  Acting on a cancel
 *     runs the thread's cleanup stack (LIFO) and TSD destructors and
 *     terminates the thread with PTHREAD_CANCELED (via pthread_exit).
 *
 * Limitation: because substrate blocking calls (sleep, read, ...) are not
 * cancellation points and thread-directed signals do not interrupt+dispatch,
 * a cancel is only observed once the target reaches a cancellation point.
 *
 * Other pthread functions live in pthread_create.c, pthread_mutex.c,
 * pthread_cond.c, pthread_extra.c, pthread_barrier.c, pthread_spin.c.
 */
#include "pthread.h"
#include "pthread_internal.h"
#include <errno.h>

/* Per-thread cancellation state.  Zero-initialised TLS gives the POSIX
 * defaults: cancel_state = PTHREAD_CANCEL_ENABLE (0), cancel_type =
 * PTHREAD_CANCEL_DEFERRED (0), an empty cleanup stack. */
static __thread int cancel_state __attribute__((tls_model("initial-exec")));
static __thread int cancel_type  __attribute__((tls_model("initial-exec")));
static __thread int in_cancel    __attribute__((tls_model("initial-exec")));
static __thread struct __pthread_cleanup *cleanup_stack
    __attribute__((tls_model("initial-exec")));

/* Run the calling thread's cleanup handlers, top of stack first.  Exposed for
 * pthread_exit() (pthread_create.c); pop each entry before running it so a
 * handler that itself calls pthread_exit doesn't re-run the same entry. */
void __pthread_run_cleanup_handlers(void) {
    struct __pthread_cleanup *c = cleanup_stack;
    while (c) {
        cleanup_stack = c->__next;
        c->__routine(c->__arg);
        c = cleanup_stack;
    }
}

void __pthread_cleanup_push(struct __pthread_cleanup *buf,
                            void (*routine)(void *), void *arg) {
    buf->__routine = routine;
    buf->__arg     = arg;
    buf->__next    = cleanup_stack;
    cleanup_stack  = buf;
}

void __pthread_cleanup_pop(struct __pthread_cleanup *buf, int execute) {
    /* The matching push made `buf` the top of the stack; unlink it.  Guard
     * against a mismatched pop leaving the stack pointing at a stale frame. */
    if (cleanup_stack == buf)
        cleanup_stack = buf->__next;
    if (execute)
        buf->__routine(buf->__arg);
}

/* Terminate the calling thread in response to a cancel: run cleanup handlers
 * + TSD destructors (via pthread_exit) and exit with PTHREAD_CANCELED. */
static void do_cancel(void) {
    if (in_cancel)
        return;                 /* already unwinding */
    in_cancel = 1;
    __pthread_cancel_consume();
    pthread_exit(PTHREAD_CANCELED);
}

void pthread_testcancel(void) {
    if (cancel_state == PTHREAD_CANCEL_ENABLE && __pthread_cancel_requested())
        do_cancel();
}

int pthread_setcancelstate(int state, int *oldstate) {
    if (state != PTHREAD_CANCEL_ENABLE && state != PTHREAD_CANCEL_DISABLE)
        return EINVAL;
    if (oldstate)
        *oldstate = cancel_state;
    cancel_state = state;
    /* Re-enabling with an async cancel already pending acts at once. */
    if (state == PTHREAD_CANCEL_ENABLE &&
        cancel_type == PTHREAD_CANCEL_ASYNCHRONOUS &&
        __pthread_cancel_requested())
        do_cancel();
    return 0;
}

int pthread_setcanceltype(int type, int *oldtype) {
    if (type != PTHREAD_CANCEL_DEFERRED && type != PTHREAD_CANCEL_ASYNCHRONOUS)
        return EINVAL;
    if (oldtype)
        *oldtype = cancel_type;
    cancel_type = type;
    /* Switching to async while enabled with a pending cancel acts at once. */
    if (type == PTHREAD_CANCEL_ASYNCHRONOUS &&
        cancel_state == PTHREAD_CANCEL_ENABLE &&
        __pthread_cancel_requested())
        do_cancel();
    return 0;
}

int pthread_cancel(pthread_t thread) {
    /* Post the cancel to the target thread's registry node (no signal). */
    return __pthread_post_cancel(thread);
}
