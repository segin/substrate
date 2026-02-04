#include <kern/console.h>
#include <sys/kthread.h>
#include <kern/sched.h>
#include "tests.h"

static void dummy_thread_func(void *arg) {
    kprintf("Dummy thread running with arg: %s\n", (char*)arg);
    kthread_exit();
}

void run_kthread_create_tests(void) {
    kprint("TEST: Checking kthread_create...\n");

    thread_t *t = NULL;
    int res = kthread_create(dummy_thread_func, "test_arg", &t, "dummy_thread");

    if (res == 0) {
        kprint("PASS: kthread_create returned success\n");
    } else {
        kprintf("FAIL: kthread_create returned failure: %d\n", res);
        return;
    }

    if (t != NULL) {
        kprint("PASS: thread pointer is not NULL\n");
        if (t->tid > 0) {
            kprintf("PASS: thread ID is valid: %d\n", t->tid);
        } else {
            kprintf("FAIL: thread ID is invalid: %d\n", t->tid);
        }
    } else {
        kprint("FAIL: thread pointer is NULL\n");
    }
}
