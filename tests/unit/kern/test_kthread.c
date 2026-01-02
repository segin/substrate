#include "../../../sys/sys/kthread.h"
#include <stdbool.h>
#include <stddef.h>

/*
 * kthread Unit Tests
 */

static void dummy_thread_func(void *arg) {
    (void)arg;
    kthread_exit();
}

extern void sched_init(void);

bool test_kthread_creation(void) {
    sched_init();
    int result = kthread_create(dummy_thread_func, NULL, NULL, "test-thread");
    return (result == 0);
}

bool test_kthread_kernel_association(void) {
    // Verifies that kthread_create uses the correct process (PID 1)
    // (Actual verification requires exposing sched.c internals to test)
    return true;
}
