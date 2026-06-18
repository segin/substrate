/* pthread_create / pthread_exit / pthread_join, plus the in-process
 * thread registry and creation trampoline.  Other pthread functions live
 * in pthread_mutex.c, pthread_cond.c, pthread_sig.c. */

#include "pthread.h"
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/thr.h>
#include <stdint.h>

/*
 * Per-thread bookkeeping is a lock-protected singly-linked list of
 * malloc'd nodes — NOT a fixed-size table.  The old thread_table[64]
 * imposed an artificial 64-threads-per-process ceiling (the kernel has
 * none: tids are a ~10M global space).  Heavily-threaded clients —
 * a TDE app, an X server, a connection-per-thread daemon — blew through
 * it and pthread_create() failed with the table full.  Nodes are
 * malloc'd and never move, so a node pointer handed to the creation
 * trampoline stays valid for the thread's whole life; a node is freed
 * only after the kernel thread is provably dead (SYS_THR_JOIN), so there
 * is no use-after-free against the still-running thread.
 */
struct pthread_info {
    pthread_t tid;
    void *exit_status;          /* return value, for join */
    int exited;                 /* start_routine returned — reapable */
    int detached;               /* pthread_detach: reap on exit, never joined */
    void *stack;
    size_t stack_size;
    struct pthread_info *next;
};

static struct pthread_info *thread_list = NULL;   /* lock-protected */
static int thread_table_lock = 0;

#define TT_LOCK()   do { while (__sync_lock_test_and_set(&thread_table_lock, 1)) ; } while (0)
#define TT_UNLOCK() __sync_lock_release(&thread_table_lock)

/* caller holds thread_table_lock */
static struct pthread_info *ti_find_locked(pthread_t tid) {
    for (struct pthread_info *n = thread_list; n; n = n->next)
        if (n->tid == tid) return n;
    return NULL;
}

/* caller holds thread_table_lock */
static void ti_unlink_locked(struct pthread_info *ti) {
    struct pthread_info **pp = &thread_list;
    while (*pp) {
        if (*pp == ti) { *pp = ti->next; ti->next = NULL; return; }
        pp = &(*pp)->next;
    }
}

struct trampoline_args {
    void *(*start_routine)(void *);
    void *arg;
    struct pthread_info *ti;     /* stable node pointer, for exit bookkeeping */
};

/*
 * Reclaim detached threads that have finished.  A detached thread is
 * never joined by the caller, so its stack and registry node would
 * otherwise leak.  Called from pthread_create() so resources are
 * recycled as connections (telnetd) churn.  SYS_THR_JOIN here is used
 * purely as "block until the kernel thread is truly gone" — it is safe
 * to free the stack only once nothing runs on it.
 */
static void pthread_reap_detached(void) {
    for (;;) {
        struct pthread_info *ti = NULL;
        TT_LOCK();
        for (struct pthread_info *n = thread_list; n; n = n->next) {
            if (n->detached && n->exited) { ti = n; ti_unlink_locked(n); break; }
        }
        TT_UNLOCK();
        if (!ti) break;
        syscall(SYS_THR_JOIN, ti->tid, 0);   /* wait until truly dead */
        free(ti->stack);
        free(ti);
    }
}

/* Thread bootstrap wrapper */
void __pthread_trampoline(void *arg) {
    struct trampoline_args *ta = (struct trampoline_args *)arg;
    void *(*start_routine)(void *) = ta->start_routine;
    void *actual_arg = ta->arg;
    struct pthread_info *ti = ta->ti;
    free(ta);

    void *retval = start_routine(actual_arg);

    /* Mark finished so a detached thread's stack + node get reclaimed by
     * the next reap sweep, and a joiner can collect the status.  `ti` is
     * a stable malloc'd node; nothing frees it until this thread is dead. */
    TT_LOCK();
    ti->exit_status = retval;
    ti->exited = 1;
    TT_UNLOCK();

    pthread_exit(retval);
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start_routine) (void *), void *arg) {
    (void)attr;

    /* Recycle any detached threads that have finished, so churning
     * callers don't accumulate dead nodes. */
    pthread_reap_detached();

    struct pthread_info *ti = calloc(1, sizeof(*ti));
    if (!ti) return -1;

    ti->stack_size = 64 * 1024;
    ti->stack = malloc(ti->stack_size);
    if (!ti->stack) { free(ti); return -1; }

    struct trampoline_args *ta = malloc(sizeof(struct trampoline_args));
    if (!ta) { free(ti->stack); free(ti); return -1; }
    ta->start_routine = start_routine;
    ta->arg = arg;
    ta->ti = ti;

    /* Per-thread TLS: ask ld.so to allocate a fresh block laid out the
     * same as the initial thread's, with each module's PT_TLS image
     * copied into place.  Returned pointer is the TP (= TCB address); the
     * kernel installs it as the new thread's gs_base so `mov %gs:0, %eax`
     * and TLS-relative loads inside the new thread find this thread's own
     * data.
     *
     * Resolved as a weak ref so a statically-linked program without ld.so
     * (no TLS, or all TLS in the initial thread) still links — in that
     * case __ldso_alloc_tls is NULL and we pass tls_base=NULL, which the
     * kernel treats as "inherit / no change". */
    extern void *__ldso_alloc_tls(void) __attribute__((weak));
    void *tp = __ldso_alloc_tls ? __ldso_alloc_tls() : NULL;

    /* Link the node before spawning so a joiner/reaper can find it as
     * soon as the kernel publishes the tid (via child_tid below).  tid is
     * 0 until then, which no real tid matches, so an early lookup is a
     * harmless miss. */
    TT_LOCK();
    ti->next = thread_list;
    thread_list = ti;
    TT_UNLOCK();

    struct thr_param param;
    param.start_func = (void(*)(void*))(uintptr_t)__pthread_trampoline;
    param.arg = ta;
    param.stack_base = ti->stack;
    param.stack_size = ti->stack_size;
    param.tls_base = tp;
    param.tls_size = 0;        /* informational, kernel ignores */
    param.child_tid = (long*)&ti->tid;
    param.parent_tid = NULL;
    param.flags = 0;

    int ret = (int)syscall(SYS_THR_NEW, (intptr_t)&param, sizeof(param));

    if (ret != 0) {
        TT_LOCK();
        ti_unlink_locked(ti);
        TT_UNLOCK();
        free(ta);
        free(ti->stack);
        free(ti);
        return -1;
    }

    if (thread) *thread = ti->tid;
    return 0;
}

void pthread_exit(void *retval) {
    /* Run this thread's TSD (pthread_key) destructors before it dies — both
     * an explicit pthread_exit and a normal return through the trampoline
     * land here. */
    extern void __pthread_tsd_run_destructors(void);
    __pthread_tsd_run_destructors();
    syscall(SYS_THR_EXIT, (int)(uintptr_t)retval);
    /* Should not be reached */
    _exit(0);
}

int pthread_join(pthread_t thread, void **retval) {
    /* Confirm it's one of ours (and joinable) before blocking. */
    TT_LOCK();
    struct pthread_info *ti = ti_find_locked(thread);
    int detached = ti ? ti->detached : 0;
    TT_UNLOCK();

    if (!ti) return ESRCH;
    if (detached) return EINVAL;        /* can't join a detached thread */

    int ret = (int)syscall(SYS_THR_JOIN, thread, (int)(uintptr_t)retval);
    if (ret != 0) return ret;

    /* Kernel thread is gone; unlink and free the node + stack. */
    TT_LOCK();
    ti = ti_find_locked(thread);        /* re-find under lock */
    if (ti) ti_unlink_locked(ti);
    TT_UNLOCK();
    if (ti) { free(ti->stack); free(ti); }

    return 0;
}

/*
 * pthread_detach — mark a thread so it is never joined: when it finishes,
 * its stack and registry node are reclaimed automatically by the reap
 * sweep in pthread_create() (or immediately here, if it has already
 * exited).
 */
int pthread_detach(pthread_t thread) {
    int rc = ESRCH;
    int reap_now = 0;

    TT_LOCK();
    struct pthread_info *ti = ti_find_locked(thread);
    if (ti) {
        ti->detached = 1;
        reap_now = ti->exited;
        rc = 0;
    }
    TT_UNLOCK();

    /* Already finished before we detached it — reclaim it right away. */
    if (rc == 0 && reap_now) pthread_reap_detached();
    return rc;
}
