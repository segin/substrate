#include <stddef.h>

#include <arch/i386/pmm.h>
#include <kern/sched.h>
#include <sys/kthread.h>
#include <sys/proc.h>

int kthread_create(void (*func)(void *), void *arg, thread_t **tdp, const char *name) {
    // 1. Threads created by kthread_create belong to the Kernel Process (PID 0)
    process_t *proc = kernel_process;

    if (!proc) {
        proc = swapper_get_proc();
    }
    if (!proc) {
        return -1;
    }

    // 2. Allocate a kernel stack — 16 KiB (4 PMM blocks).  Matches
    //    the per-process kstack size; 8 KiB overflows in the deep
    //    network TX path (see fork_kthread in pm/process.c).
    void *stack = pmm_alloc_contiguous(4);
    if (!stack) return -1;

    // 3. Use scheduler to create the thread
    thread_t *t = sched_create_thread(proc, (void (*)(void*))func, (char*)stack + 16384, arg);

    if (!t) {
        pmm_free_contiguous(stack, 4);
        return -1;
    }

    t->kstack_base = (uintptr_t)stack;
    t->kstack_units = 4;
    t->kstack_type = THREAD_KSTACK_PMM_CONTIG;
    t->kstack_owned = 1;

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
