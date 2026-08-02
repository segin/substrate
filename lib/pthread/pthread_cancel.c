/*
 * pthread_cancel.c — POSIX thread cancellation + the cleanup-handler stack.
 *
 * Cancellation is signal-driven (a directed SIGCANCEL), backed by a per-thread
 * pending flag on the registry node:
 *
 *   - A cancel is POSTED by setting a `cancel_pending` flag on the target
 *     thread's registry node (shared, lock-protected — see pthread_create.c)
 *     AND by delivering a directed SIGCANCEL to the target's kernel tid via
 *     thr_kill(2).  The signal (a) wakes the target out of an interruptible
 *     blocking syscall (sleep/nanosleep, mutex/cond/rwlock futex wait, ...)
 *     with EINTR and (b) runs the target thread's handler on its return to
 *     userspace — so an ASYNCHRONOUS cancel is acted on *while the target is
 *     blocked*, and a DEFERRED cancel makes the interrupted cancellation point
 *     act.  (Thread-directed signal delivery works in this kernel: thr_kill
 *     sets the target thread's sig_pending and signal_handle_pending() runs on
 *     that thread's syscall return.)
 *
 *   - Per-thread cancel state (ENABLE/DISABLE) and type (DEFERRED/ASYNC) and
 *     the cleanup stack live in TLS (initial-exec model — libpthread is a
 *     startup DT_NEEDED, never dlopen'd).
 *
 *   - A DEFERRED cancel is acted upon at the next cancellation point:
 *     pthread_testcancel, the state/type setters (re-enable / switch-to-async
 *     with a cancel already pending), and the sleep/nanosleep cancellation
 *     points (libc calls the weak pthread_testcancel after the syscall
 *     returns EINTR).  Acting on a cancel runs the thread's cleanup stack
 *     (LIFO) and TSD destructors and terminates the thread with
 *     PTHREAD_CANCELED (via pthread_exit).
 *
 * The SIGCANCEL handler is installed LAZILY on the first pthread_cancel(), not
 * in a library constructor: every substrate binary that links -lpthread would
 * otherwise hijack SIGRTMIN process-wide, swallowing the RT signal from
 * programs (and OPTS signals-area tests) that use it directly.  A program that
 * actually calls pthread_cancel() is a cancellation user and does not also use
 * SIGRTMIN for messaging.
 *
 * Other pthread functions live in pthread_create.c, pthread_mutex.c,
 * pthread_cond.c, pthread_extra.c, pthread_barrier.c, pthread_spin.c.
 */
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include <pthread_internal.h>
#include <sys/syscall.h>
#include <sys/thr.h>

/* Directed cancellation signal.  substrate's RT range is {SIGRTMIN=29,
 * SIGRTMAX=30}; SIGCANCEL borrows SIGRTMIN (glibc convention). */
#ifndef SIGCANCEL
#define SIGCANCEL SIGRTMIN
#endif

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

/*
 * SIGCANCEL handler — runs in the *target* thread's context after thr_kill
 * wakes it and signal_handle_pending() delivers on its return to userspace.
 * SIGCANCEL is a dedicated internal signal only ever raised by pthread_cancel,
 * so its arrival means a cancel was requested for this thread; we consult only
 * the TLS cancel state/type (never the registry lock — a signal handler must
 * not spin on a lock the interrupted context may hold).
 */
static void cancel_signal_handler(int sig) {
    (void)sig;
    if (in_cancel)
        return;                         /* already unwinding */
    if (cancel_state != PTHREAD_CANCEL_ENABLE)
        return;                         /* disabled: cancel stays pending */
    if (cancel_type == PTHREAD_CANCEL_ASYNCHRONOUS)
        do_cancel();                    /* act now — never returns */
    /* DEFERRED: return.  The registry pending flag is already set (below), so
     * the interrupted cancellation point acts: the blocking syscall returns
     * EINTR and sleep/nanosleep's testcancel hook (or an explicit
     * pthread_testcancel) calls do_cancel(). */
}

/* Install the SIGCANCEL handler once, process-wide.  sa_flags = 0 (no
 * SA_RESTART) so the signal interrupts the target's blocking syscall with
 * EINTR instead of restarting it. */
static volatile int cancel_handler_installed;
static int cancel_install_lock;
static void ensure_cancel_handler(void) {
    if (cancel_handler_installed)
        return;
    while (__sync_lock_test_and_set(&cancel_install_lock, 1))
        ;
    if (!cancel_handler_installed) {
        struct sigaction sa;
        sa.sa_handler = cancel_signal_handler;
        sa.sa_flags   = 0;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGCANCEL, &sa, NULL);
        cancel_handler_installed = 1;
    }
    __sync_lock_release(&cancel_install_lock);
}

int pthread_cancel(pthread_t thread) {
    ensure_cancel_handler();
    /* Record the request on the target's registry node.  This survives even
     * if the target is not currently in an interruptible syscall, and is what
     * a DEFERRED cancellation point later reads. */
    int rc = __pthread_post_cancel(thread);
    if (rc != 0)
        return rc;                      /* ESRCH: no such thread */
    /* Nudge the target with a directed SIGCANCEL: wakes it out of an
     * interruptible block (EINTR) and runs cancel_signal_handler() on its
     * return to userspace.  Best-effort — if the thread already exited the
     * post above still recorded the request for a joiner. */
    syscall(SYS_THR_KILL, (long)thread, SIGCANCEL);
    return 0;
}
