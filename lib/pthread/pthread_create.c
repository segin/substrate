/* pthread_create / pthread_exit / pthread_join, plus the in-process
 * thread table and creation trampoline.  Other pthread functions live
 * in pthread_mutex.c, pthread_cond.c, pthread_sig.c. */

#include "pthread.h"
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/thr.h>
#include <stdint.h>

#define MAX_PTHREADS 64

struct pthread_info {
    pthread_t tid;
    void *exit_status; /* Thread exit status for join */
    int exited;        /* start_routine returned — slot is reapable */
    int detached;      /* pthread_detach: no join; reap on exit */
    int used;
    void *stack;
    size_t stack_size;
};

static struct pthread_info thread_table[MAX_PTHREADS];
static int thread_table_lock = 0;

struct trampoline_args {
    void *(*start_routine)(void *);
    void *arg;
    int   slot;        /* index into thread_table, for exit bookkeeping */
};

/*
 * Reclaim detached threads that have finished.  A detached thread is
 * never joined by the caller, so its 64 KiB stack and table slot
 * would otherwise leak.  Called from pthread_create() so resources
 * are recycled as connections (telnetd) churn.  SYS_THR_JOIN here is
 * used purely as "block until the kernel thread is truly gone" — it
 * is safe to free the stack only once nothing runs on it.
 */
static void pthread_reap_detached(void) {
    for (int i = 0; i < MAX_PTHREADS; i++) {
        pthread_t tid = 0;
        void *stack = NULL;

        while (__sync_lock_test_and_set(&thread_table_lock, 1));
        struct pthread_info *ti = &thread_table[i];
        if (ti->used && ti->detached && ti->exited) {
            tid   = ti->tid;
            stack = ti->stack;
            ti->used = 0; ti->tid = 0; ti->stack = NULL;
            ti->detached = 0; ti->exited = 0; ti->exit_status = NULL;
        }
        __sync_lock_release(&thread_table_lock);

        if (stack) {
            syscall(SYS_THR_JOIN, tid, 0);   /* wait until truly dead */
            free(stack);
        }
    }
}

/* Thread bootstrap wrapper */
void __pthread_trampoline(void *arg) {
    struct trampoline_args *ta = (struct trampoline_args *)arg;
    void *(*start_routine)(void *) = ta->start_routine;
    void *actual_arg = ta->arg;
    int slot = ta->slot;
    free(ta);

    void *retval = start_routine(actual_arg);

    /* Mark the slot finished so a detached thread's stack + slot get
     * reclaimed by the next pthread_create() reap sweep. */
    if (slot >= 0 && slot < MAX_PTHREADS) {
        while (__sync_lock_test_and_set(&thread_table_lock, 1));
        thread_table[slot].exit_status = retval;
        thread_table[slot].exited = 1;
        __sync_lock_release(&thread_table_lock);
    }

    pthread_exit(retval);
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start_routine) (void *), void *arg) {
    (void)attr;

    /* Recycle any detached threads that have finished before we look
     * for a slot, so churning callers don't exhaust the table. */
    pthread_reap_detached();

    int slot = -1;
    while (__sync_lock_test_and_set(&thread_table_lock, 1));
    for (int i = 0; i < MAX_PTHREADS; i++) {
        if (!thread_table[i].used) {
            slot = i;
            thread_table[i].used = 1;
            break;
        }
    }
    __sync_lock_release(&thread_table_lock);

    if (slot == -1) return -1;

    struct pthread_info *ti = &thread_table[slot];
    ti->exited = 0;
    ti->detached = 0;
    ti->exit_status = NULL;

    ti->stack_size = 64 * 1024;
    ti->stack = malloc(ti->stack_size);
    if (!ti->stack) {
        while (__sync_lock_test_and_set(&thread_table_lock, 1));
        ti->used = 0;
        __sync_lock_release(&thread_table_lock);
        return -1;
    }

    struct trampoline_args *ta = malloc(sizeof(struct trampoline_args));
    if (!ta) {
        free(ti->stack);
        while (__sync_lock_test_and_set(&thread_table_lock, 1));
        ti->used = 0;
        __sync_lock_release(&thread_table_lock);
        return -1;
    }
    ta->start_routine = start_routine;
    ta->arg = arg;
    ta->slot = slot;

    /* Per-thread TLS: ask ld.so to allocate a fresh block laid out
     * the same as the initial thread's, with each module's PT_TLS
     * image copied into place.  Returned pointer is the TP (= TCB
     * address); the kernel installs it as the new thread's
     * gs_base so `mov %gs:0, %eax` and TLS-relative loads inside
     * the new thread find this thread's own data.
     *
     * Resolved as a weak ref so a statically-linked program
     * without ld.so (no TLS, or all TLS in the initial thread)
     * still links — in that case __ldso_alloc_tls is NULL and we
     * pass tls_base=NULL, which the kernel treats as "inherit / no
     * change".  Threads in such a program don't have proper TLS,
     * which matches the pre-this-commit behaviour. */
    extern void *__ldso_alloc_tls(void) __attribute__((weak));
    void *tp = __ldso_alloc_tls ? __ldso_alloc_tls() : NULL;

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
        free(ta);
        free(ti->stack);
        while (__sync_lock_test_and_set(&thread_table_lock, 1));
        ti->used = 0;
        __sync_lock_release(&thread_table_lock);
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
    /* Find the thread table entry to cleanup resources */
    int slot = -1;
    /* Optimistic search without lock - TID shouldn't be reused while we hold a reference/join it */
    for (int i = 0; i < MAX_PTHREADS; i++) {
        if (thread_table[i].used && thread_table[i].tid == thread) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        return ESRCH;
    }

    if (thread_table[slot].detached) return EINVAL;  /* can't join detached */

    int ret = (int)syscall(SYS_THR_JOIN, thread, (int)(uintptr_t)retval);
    if (ret != 0) return ret;

    if (slot != -1) {
        struct pthread_info *ti = &thread_table[slot];

        /* Mark slot as free safely and free resources */
        while (__sync_lock_test_and_set(&thread_table_lock, 1));

        /* Re-check usage inside lock in case of race (unlikely given tid match) */
        if (ti->used && ti->tid == thread) {
            if (ti->stack) {
                free(ti->stack);
                ti->stack = NULL;
            }
            ti->used = 0;
            ti->tid = 0;
            ti->exited = 0;
            ti->exit_status = NULL;
        }

        __sync_lock_release(&thread_table_lock);
    }

    return 0;
}

/*
 * pthread_detach — mark a thread so it is never joined: when it
 * finishes, its stack and table slot are reclaimed automatically by
 * the reap sweep in pthread_create() (or immediately here, if it has
 * already exited).
 */
int pthread_detach(pthread_t thread) {
    int rc = ESRCH;
    int reap_now = 0;

    while (__sync_lock_test_and_set(&thread_table_lock, 1));
    for (int i = 0; i < MAX_PTHREADS; i++) {
        if (thread_table[i].used && thread_table[i].tid == thread) {
            thread_table[i].detached = 1;
            reap_now = thread_table[i].exited;
            rc = 0;
            break;
        }
    }
    __sync_lock_release(&thread_table_lock);

    /* Already finished before we detached it — reclaim it right away. */
    if (rc == 0 && reap_now) pthread_reap_detached();
    return rc;
}
