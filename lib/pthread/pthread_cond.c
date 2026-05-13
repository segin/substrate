#include "pthread.h"
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/syscall.h>
#include <sys/futex.h>
#include <sys/thr.h>
#include <stdint.h>

#define MAX_PTHREADS 64

struct pthread_info {
    pthread_t tid;
    void *exit_status; // Thread exit status for join
    int exited;
    int used;
    void *stack;
    size_t stack_size;
};

static struct pthread_info thread_table[MAX_PTHREADS];
static int thread_table_lock = 0;

struct trampoline_args {
    void *(*start_routine)(void *);
    void *arg;
};

// Thread bootstrap wrapper
void __pthread_trampoline(void *arg) {
    struct trampoline_args *ta = (struct trampoline_args *)arg;
    void *(*start_routine)(void *) = ta->start_routine;
    void *actual_arg = ta->arg;
    free(ta);

    void *retval = start_routine(actual_arg);
    pthread_exit(retval);
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start_routine) (void *), void *arg) {
    (void)attr;
    
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

// Mutex stubs
int pthread_mutex_init(pthread_mutex_t *mutex, const void *attr) { (void)attr; *mutex = 0; return 0; }
int pthread_mutex_lock(pthread_mutex_t *mutex) {
    // Spinlock for now (unsafe if single core without preemption)
    while (__sync_lock_test_and_set(mutex, 1)) {
        // yield();
    }
    return 0;
}
int pthread_mutex_unlock(pthread_mutex_t *mutex) { __sync_lock_release(mutex); return 0; }
int pthread_mutex_destroy(pthread_mutex_t *mutex) { (void)mutex; return 0; }

/* ---- Thread-identity + per-thread signal delivery ---- */

pthread_t pthread_self(void) {
    return (pthread_t)syscall(SYS_THR_SELF);
}

int pthread_kill(pthread_t thread, int sig) {
    long rc = syscall(SYS_THR_KILL, (long)thread, sig);
    return rc < 0 ? (int)-rc : 0;
}

int pthread_sigmask(int how, const sigset_t *set, sigset_t *oldset) {
    /* Substrate's sig_mask is per-thread (thread_t.sig_mask), so
     * sigprocmask already does the per-thread thing in this kernel.
     * pthread_sigmask is just a renamed wrapper for portability. */
    extern int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
    return sigprocmask(how, set, oldset) == 0 ? 0 : errno;
}

/* ---- Condition variables ----
 *
 * Sequence-number cond_var, futex-backed.  Standard Linux glibc
 * shape:
 *
 *   wait:   record seq under mutex, unlock mutex, FUTEX_WAIT on
 *           &cond->seq with the recorded value (kernel atomically
 *           parks iff seq still == recorded), then re-lock mutex.
 *   signal: bump seq, FUTEX_WAKE 1.
 *   broadcast: bump seq, FUTEX_WAKE INT_MAX.
 *
 * Two known limitations of this minimum-viable shape:
 *
 *   - pthread_cond_timedwait isn't here yet.  The torture suite
 *     doesn't use it.
 *
 *   - Spurious wakeups: FUTEX_WAIT can return early; per POSIX,
 *     pthread_cond_wait callers must re-check the predicate.
 *     We don't add a retry loop here — the suite already follows
 *     the predicate-while pattern.
 */

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) {
    (void)attr;
    cond->seq = 0;
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond) {
    (void)cond;
    return 0;
}

int pthread_cond_signal(pthread_cond_t *cond) {
    __atomic_add_fetch(&cond->seq, 1, __ATOMIC_SEQ_CST);
    syscall(SYS_FUTEX, (long)&cond->seq, FUTEX_WAKE, 1, 0, 0, 0);
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t *cond) {
    __atomic_add_fetch(&cond->seq, 1, __ATOMIC_SEQ_CST);
    /* INT_MAX waiters — kernel iterates the futex wait queue. */
    syscall(SYS_FUTEX, (long)&cond->seq, FUTEX_WAKE, 0x7fffffff, 0, 0, 0);
    return 0;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
    int old_seq = __atomic_load_n(&cond->seq, __ATOMIC_SEQ_CST);
    pthread_mutex_unlock(mutex);
    /* Atomic check-and-park inside the kernel: FUTEX_WAIT only
     * parks if *addr still == old_seq.  If another thread already
     * signaled (incrementing seq) between our load and the syscall,
     * the wait returns immediately with EAGAIN and we re-acquire
     * the mutex with the new state visible — which is exactly what
     * cond_wait must guarantee to avoid lost wakeups. */
    syscall(SYS_FUTEX, (long)&cond->seq, FUTEX_WAIT, old_seq, 0, 0, 0);
    pthread_mutex_lock(mutex);
    return 0;
}

/* pthread_cond_timedwait — same as cond_wait but with an absolute
 * CLOCK_REALTIME deadline.  Returns 0 on signal, ETIMEDOUT on
 * deadline elapsed.  Substrate's FUTEX_WAIT takes a RELATIVE
 * timespec, so we convert abstime - now and pass that. */
int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *abstime) {
    if (!abstime) return pthread_cond_wait(cond, mutex);

    int old_seq = __atomic_load_n(&cond->seq, __ATOMIC_SEQ_CST);
    pthread_mutex_unlock(mutex);

    long rc = 0;
    for (;;) {
        struct timespec now, rel;
        if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
            rc = -EINVAL;
            break;
        }
        /* relative = abstime - now */
        rel.tv_sec  = abstime->tv_sec  - now.tv_sec;
        rel.tv_nsec = abstime->tv_nsec - now.tv_nsec;
        if (rel.tv_nsec < 0) {
            rel.tv_sec  -= 1;
            rel.tv_nsec += 1000000000L;
        }
        if (rel.tv_sec < 0 || (rel.tv_sec == 0 && rel.tv_nsec <= 0)) {
            rc = -ETIMEDOUT;
            break;
        }

        rc = syscall(SYS_FUTEX, (long)&cond->seq, FUTEX_WAIT, old_seq,
                     (long)&rel, 0, 0);
        if (rc == 0)                 break;   /* normal wake */
        if (rc == -EAGAIN)            { rc = 0; break; }   /* seq moved — proceed */
        if (rc == -ETIMEDOUT)         break;
        if (rc == -EINTR)             continue; /* spurious; retry with fresh delta */
        break;
    }

    pthread_mutex_lock(mutex);
    if (rc == -ETIMEDOUT) return ETIMEDOUT;
    if (rc < 0)           return (int)-rc;
    return 0;
}
