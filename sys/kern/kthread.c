#include "../sys/kthread.h"
#include "../sys/proc.h"
#include "../kern/sched.h"
#include "../vm/vm_kmem.h"
#include <stddef.h>

extern process_t processes[]; // Accessible from sched.c

int kthread_create(void (*func)(void *), void *arg, thread_t **tdp, const char *name) {
    // 1. All kthreads belong to kernel process (PID 1)
    process_t *proc = &processes[0]; 

    // 2. Allocate a kernel stack
    void *stack = kmalloc(4096);
    if (!stack) return -1;

    // 3. Use scheduler to create the thread
    int tid = sched_create_thread(proc, (void (*)(void*))func, (char*)stack + 4096, arg);
    
    if (tid < 0) {
        kfree(stack, 4096);
        return -1;
    }

    // 4. (Optional) set thread name
    (void)name;

    // 5. Success
    if (tdp) {
        // TODO: Find the thread_t by tid and return it
    }

    return 0;
}

void kthread_exit(void) {
    if (current_thread) {
        current_thread->state = THREAD_ZOMBIE;
        sched_yield();
    }
}
