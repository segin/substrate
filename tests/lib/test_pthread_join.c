#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/thr.h>

long mock_syscall(long number, ...) {
    va_list args;
    va_start(args, number);
    if (number == SYS_THR_JOIN) {
        long tid = va_arg(args, long);
        if (tid == 1) {
            va_end(args);
            return 0; // success
        } else if (tid == 2) {
            // Already joined/invalid from kernel's perspective
            va_end(args);
            return ESRCH;
        } else {
            va_end(args);
            return ESRCH;
        }
    } else if (number == SYS_THR_NEW) {
        va_end(args);
        return 0;
    }
    va_end(args);
    return 0;
}

/*
 * pthread_exit() calls into the cancellation and TSD units, which this test
 * does not link -- it is about join, not teardown.  Signatures per
 * lib/pthread/pthread_internal.h.
 */
void __pthread_run_cleanup_handlers(void) { }
void __pthread_tsd_run_destructors(void) { }

#define syscall mock_syscall
/* libpthread was split into per-feature translation units; pthread_join
 * and pthread_create now live in pthread_create.c. */
#include "../../lib/pthread/pthread_create.c"

bool test_pthread_join_invalid_thread() {
    void *retval;

    // Test joining an invalid thread (ESRCH from kernel)
    int ret = pthread_join(9999, &retval);

    if (ret != ESRCH) {
        printf("pthread_join invalid thread returned %d (expected %d)\n", ret, ESRCH);
        return false;
    }

    return true;
}

bool test_pthread_join_valid_thread() {
    void *retval;

    /*
     * libpthread's bookkeeping used to be a fixed thread_table[] with a
     * `used` flag; it is now a linked list of struct pthread_info hanging
     * off thread_list.  Build a joinable node by hand and link it in, which
     * is what pthread_create() would have left behind.
     */
    struct pthread_info *ti = calloc(1, sizeof(*ti));
    if (!ti) {
        printf("pthread_join valid thread: out of memory\n");
        return false;
    }
    ti->tid = 1;
    ti->stack = malloc(1024);
    ti->stack_size = 1024;
    ti->exited = 1;
    ti->next = thread_list;
    thread_list = ti;

    int ret = pthread_join(1, &retval);
    if (ret != 0) {
        printf("pthread_join valid thread returned %d (expected 0)\n", ret);
        return false;
    }

    /*
     * On success pthread_join unlinks the node and frees it along with the
     * stack, so the node itself must not be touched again -- ask the list
     * instead.
     */
    if (ti_find_locked(1) != NULL) {
        printf("pthread_join valid thread left its node on the list\n");
        return false;
    }

    return true;
}


int main() {
    bool passed = true;
    printf("test_pthread_join_invalid_thread: ");
    if (test_pthread_join_invalid_thread()) {
        printf("PASS\n");
    } else {
        printf("FAIL\n");
        passed = false;
    }

    printf("test_pthread_join_valid_thread: ");
    if (test_pthread_join_valid_thread()) {
        printf("PASS\n");
    } else {
        printf("FAIL\n");
        passed = false;
    }

    return passed ? 0 : 1;
}
