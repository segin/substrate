#include "pthread.h"
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
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
