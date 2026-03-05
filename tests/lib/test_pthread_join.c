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

#define syscall mock_syscall
#include "../../lib/pthreads/pthread.c"

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

    // Simulate thread in table
    thread_table[0].used = 1;
    thread_table[0].tid = 1;
    thread_table[0].stack = malloc(1024);

    int ret = pthread_join(1, &retval);
    if (ret != 0) {
        printf("pthread_join valid thread returned %d (expected 0)\n", ret);
        return false;
    }

    if (thread_table[0].used != 0 || thread_table[0].stack != NULL) {
        printf("pthread_join valid thread did not clean up table entry properly\n");
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
