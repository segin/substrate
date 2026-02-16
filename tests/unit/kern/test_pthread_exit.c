#include <kern/sched.h>
#include <sys/syscall_impl.h>
#include <vm/vm_kmem.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

static void *test_retval = (void*)0xDEADBEEF;

// Declare internal scheduler function
extern thread_t *sched_alloc_thread(process_t *proc);

bool test_pthread_exit_logic(void) {
    // Note: In the mock environment, current_process is usually set up by sched_init
    if (!current_process) {
        // If not initialized, try to initialize
        sched_init();
    }
    if (!current_process) return false;

    // Allocate a new thread structure
    thread_t *t = sched_alloc_thread(current_process);
    if (!t) return false;

    // Manually set up the thread as if it's running
    t->state = THREAD_RUNNING;
    thread_t *saved_current = current_thread;
    current_thread = t;

    // Call the function we want to test
    sys_thr_exit(test_retval);

    // Verify results on the thread structure
    bool success = (t->state == THREAD_ZOMBIE) && (t->retval == test_retval);

    if (!success) {
        printf("Test failed: state=%d (expected %d), retval=%p (expected %p)\n",
               t->state, THREAD_ZOMBIE, t->retval, test_retval);
    }

    // Cleanup and Restore
    current_thread = saved_current;
    t->tid = -1; // Mark as free for next tests

    return success;
}
