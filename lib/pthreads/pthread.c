#include "pthread.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/futex.h>
#include <sys/thr.h>
#include <stdint.h>

/*
 * pthreads implementation for Substrate.
 *
 * Uses a global thread table for storing return values and synchronization.
 * Note: TIDs in this kernel are structured as (generation << 6) | index,
 * allowing O(1) mapping from TID to slot index (tid & 63).
 */

extern int64_t _syscall0(int);
extern int64_t _syscall1(int, int);
extern int64_t _syscall2(int, int, int);
extern int64_t _syscall3(int, int, int, int);
extern int64_t _syscall4(int, int, int, int, int);
extern int64_t _syscall5(int, int, int, int, int, int);
extern int64_t _syscall6(int, int, int, int, int, int, int);

#define MAX_PTHREADS 64

struct pthread_info {
    pthread_t tid;
    void *retval;
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
    ti->retval = NULL;

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

    struct thr_param param;
    param.start_func = (void(*)(void*))(uintptr_t)__pthread_trampoline;
    param.arg = ta;
    param.stack_base = ti->stack;
    param.stack_size = ti->stack_size;
    param.tls_base = NULL;
    param.tls_size = 0;
    param.child_tid = (long*)&ti->tid;
    param.parent_tid = NULL;
    param.flags = 0;
    
    int ret = (int)_syscall2(SYS_THR_NEW, (int)&param, sizeof(param));
    
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
    int tid = (int)_syscall0(SYS_THR_SELF);
    int slot = tid & (MAX_PTHREADS - 1);
    int *exited_ptr = NULL;

    while (__sync_lock_test_and_set(&thread_table_lock, 1));
    if (thread_table[slot].used && thread_table[slot].tid == tid) {
        thread_table[slot].retval = retval;
        exited_ptr = &thread_table[slot].exited;
    }
    __sync_lock_release(&thread_table_lock);

    /*
     * Kernel will set *exited_ptr = 1 and wake any waiters via futex
     * AFTER the thread has finished using its stack, preventing use-after-free.
     */
    _syscall1(SYS_THR_EXIT, (int)exited_ptr);
    while(1);
}

int pthread_join(pthread_t thread, void **retval) {
    int slot = thread & (MAX_PTHREADS - 1);
    struct pthread_info *ti = &thread_table[slot];

    while (1) {
        while (__sync_lock_test_and_set(&thread_table_lock, 1));
        if (!ti->used || ti->tid != thread) {
            __sync_lock_release(&thread_table_lock);
            return -1; // ESRCH
        }

        if (ti->exited) {
            if (retval) *retval = ti->retval;

            // Safe to release resources now that thread has truly exited
            if (ti->stack) {
                free(ti->stack);
                ti->stack = NULL;
            }
            ti->used = 0;
            ti->tid = -1;
            __sync_lock_release(&thread_table_lock);
            return 0;
        }
        __sync_lock_release(&thread_table_lock);

        // Wait for kernel to set ti->exited and wake us
        _syscall6(SYS_FUTEX, (int)&ti->exited, FUTEX_WAIT, 0, 0, 0, 0);
    }
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
