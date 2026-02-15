#include "pthread.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>

extern int _syscall1(int, int);
extern int _syscall2(int, int, int);

// FreeBSD-style param
struct thr_param {
    void    (*start_func)(void *);
    void    *arg;
    void    *stack_base;
    size_t  stack_size;
    void    *tls_base;
    size_t  tls_size;
    long    *child_tid;
    long    *parent_tid;
    int     flags;
};

// Thread bootstrap wrapper
void __pthread_trampoline(void *arg);

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start_routine) (void *), void *arg) {
    (void)attr;
    
    size_t stack_size = 64 * 1024;
    void *stack = malloc(stack_size);
    if (!stack) return -1;
    
    struct thr_param param;
    param.start_func = (void(*)(void*))(uintptr_t)start_routine; // Simplified: kernel jumps here
    param.arg = arg;
    param.stack_base = stack;
    param.stack_size = stack_size;
    param.tls_base = NULL;
    param.tls_size = 0;
    param.child_tid = (long*)thread;
    param.parent_tid = NULL;
    param.flags = 0;
    
    // In our simplified kernel implementation:
    // sched_create_thread(proc, entry, stack_top, arg)
    // entry = start_func
    // arg = arg
    // stack = stack_base (and kernel calculates top)
    
    // We pass param to syscall
    int ret = _syscall2(SYS_THR_NEW, (int)&param, sizeof(param));
    
    if (ret != 0) {
        free(stack);
        return -1;
    }
    
    return 0;
}

void __pthread_trampoline(void *arg) {
    (void)arg;
    // Not used in this version, kernel calls start_func directly
    pthread_exit(NULL);
}

void pthread_exit(void *retval) {
    _syscall1(SYS_THR_EXIT, (int)retval);
    /* Should not be reached */
    _exit(0);
}

int pthread_join(pthread_t thread, void **retval) {
    return _syscall2(SYS_THR_JOIN, thread, (int)retval);
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
