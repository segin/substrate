#include <sys/kthread.h>
#include <sys/proc.h>
#include <kern/sched.h>
#include <vm/vm_kmem.h>
#include <stddef.h>

extern process_t processes[]; // Accessible from sched.c

int kthread_create(void (*func)(void *), void *arg, thread_t **tdp, const char *name) {
    // 1. Threads created by kthread_create belong to the Kernel Process (PID 0)
    extern process_t *kernel_process;
    process_t *proc = kernel_process ? kernel_process : &processes[0]; 

    // 2. Allocate a kernel stack
    void *stack = kmalloc(4096);
    if (!stack) return -1;

    // 3. Use scheduler to create the thread
    thread_t *t = sched_create_thread(proc, (void (*)(void*))func, (char*)stack + 4096, arg);
    
    if (!t) {
        kfree(stack, 4096);
        return -1;
    }

    // 4. (Optional) set thread name
    (void)name;

    // 5. Success
    if (tdp) {
        *tdp = t;
    }

    return 0;
}

void kthread_exit(void) {
    if (current_thread) {
        current_thread->state = THREAD_ZOMBIE;
        sched_yield();
    }
}
