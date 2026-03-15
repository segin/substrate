#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>

#ifndef __i386__
#define __i386__
#endif

#include <sys/syscall.h>
#include <sys/resource.h>

static int mock_syscall_called = 0;
static long mock_syscall_number = 0;
static long mock_syscall_arg1 = 0;
static void *mock_syscall_arg2 = NULL;
static long mock_syscall_return = 0;

long mock_syscall(long number, ...) {
    va_list args;
    va_start(args, number);
    mock_syscall_called = 1;
    mock_syscall_number = number;
    mock_syscall_arg1 = va_arg(args, long);
    mock_syscall_arg2 = va_arg(args, void *);
    va_end(args);
    return mock_syscall_return;
}

#define syscall mock_syscall
#include "../../../lib/sys/getrusage.c"

bool test_getrusage_valid_args() {
    mock_syscall_called = 0;
    mock_syscall_number = 0;
    mock_syscall_arg1 = 0;
    mock_syscall_arg2 = NULL;
    mock_syscall_return = 0;

    struct rusage usage;
    int ret = getrusage(RUSAGE_SELF, &usage);

    if (ret != 0) {
        printf("getrusage returned %d (expected 0)\n", ret);
        return false;
    }

    if (mock_syscall_called != 1) {
        printf("syscall was not called exactly once\n");
        return false;
    }

    if (mock_syscall_number != SYS_GETRUSAGE) {
        printf("syscall number was %ld (expected %d)\n", mock_syscall_number, SYS_GETRUSAGE);
        return false;
    }

    if (mock_syscall_arg1 != RUSAGE_SELF) {
        printf("syscall arg1 was %ld (expected %d)\n", mock_syscall_arg1, RUSAGE_SELF);
        return false;
    }

    if (mock_syscall_arg2 != &usage) {
        printf("syscall arg2 was %p (expected %p)\n", mock_syscall_arg2, &usage);
        return false;
    }

    return true;
}

bool test_getrusage_error() {
    mock_syscall_called = 0;
    mock_syscall_number = 0;
    mock_syscall_arg1 = 0;
    mock_syscall_arg2 = NULL;
    mock_syscall_return = -EFAULT;

    struct rusage usage;
    int ret = getrusage(RUSAGE_CHILDREN, &usage);

    if (ret != -EFAULT) {
        printf("getrusage returned %d (expected %d)\n", ret, -EFAULT);
        return false;
    }

    if (mock_syscall_called != 1) {
        printf("syscall was not called exactly once\n");
        return false;
    }

    if (mock_syscall_number != SYS_GETRUSAGE) {
        printf("syscall number was %ld (expected %d)\n", mock_syscall_number, SYS_GETRUSAGE);
        return false;
    }

    if (mock_syscall_arg1 != RUSAGE_CHILDREN) {
        printf("syscall arg1 was %ld (expected %d)\n", mock_syscall_arg1, RUSAGE_CHILDREN);
        return false;
    }

    if (mock_syscall_arg2 != &usage) {
        printf("syscall arg2 was %p (expected %p)\n", mock_syscall_arg2, &usage);
        return false;
    }

    return true;
}

int main() {
    bool passed = true;
    printf("test_getrusage_valid_args: ");
    if (test_getrusage_valid_args()) {
        printf("PASS\n");
    } else {
        printf("FAIL\n");
        passed = false;
    }

    printf("test_getrusage_error: ");
    if (test_getrusage_error()) {
        printf("PASS\n");
    } else {
        printf("FAIL\n");
        passed = false;
    }

    return passed ? 0 : 1;
}
