#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>

#ifndef __i386__
#define __i386__
#endif

#include <sys/random.h>
#include <sys/syscall.h>

static int mock_syscall_called = 0;
static long mock_syscall_number = 0;
static void *mock_syscall_arg1 = NULL;
static long mock_syscall_arg2 = 0;
static unsigned int mock_syscall_arg3 = 0;
static long mock_syscall_return = 0;

long mock_syscall(long number, ...) {
    va_list args;

    va_start(args, number);
    mock_syscall_called = 1;
    mock_syscall_number = number;
    mock_syscall_arg1 = va_arg(args, void *);
    mock_syscall_arg2 = va_arg(args, long);
    mock_syscall_arg3 = va_arg(args, unsigned int);
    va_end(args);
    return mock_syscall_return;
}

#define syscall mock_syscall
#include "../../../lib/sys/getrandom.c"

static bool test_getrandom_valid_args(void) {
    unsigned char buf[32];
    ssize_t ret;

    mock_syscall_called = 0;
    mock_syscall_number = 0;
    mock_syscall_arg1 = NULL;
    mock_syscall_arg2 = 0;
    mock_syscall_arg3 = 0;
    mock_syscall_return = sizeof(buf);

    ret = getrandom(buf, sizeof(buf), GRND_NONBLOCK | GRND_INSECURE);
    if (ret != (ssize_t)sizeof(buf)) {
        printf("getrandom returned %ld (expected %zu)\n", (long)ret, sizeof(buf));
        return false;
    }

    if (mock_syscall_called != 1) {
        printf("syscall was not called exactly once\n");
        return false;
    }

    if (mock_syscall_number != SYS_GETRANDOM) {
        printf("syscall number was %ld (expected %d)\n", mock_syscall_number, SYS_GETRANDOM);
        return false;
    }

    if (mock_syscall_arg1 != buf) {
        printf("syscall arg1 was %p (expected %p)\n", mock_syscall_arg1, (void *)buf);
        return false;
    }

    if (mock_syscall_arg2 != (long)sizeof(buf)) {
        printf("syscall arg2 was %ld (expected %zu)\n", mock_syscall_arg2, sizeof(buf));
        return false;
    }

    if (mock_syscall_arg3 != (GRND_NONBLOCK | GRND_INSECURE)) {
        printf("syscall arg3 was %u (expected %u)\n", mock_syscall_arg3,
               GRND_NONBLOCK | GRND_INSECURE);
        return false;
    }

    return true;
}

static bool test_getrandom_error_passthrough(void) {
    unsigned char buf[8];
    ssize_t ret;

    mock_syscall_called = 0;
    mock_syscall_number = 0;
    mock_syscall_arg1 = NULL;
    mock_syscall_arg2 = 0;
    mock_syscall_arg3 = 0;
    mock_syscall_return = -11;

    ret = getrandom(buf, sizeof(buf), GRND_RANDOM);
    if (ret != -11) {
        printf("getrandom returned %ld (expected -11)\n", (long)ret);
        return false;
    }

    if (mock_syscall_called != 1) {
        printf("syscall was not called exactly once\n");
        return false;
    }

    return true;
}

int main(void) {
    bool passed = true;

    printf("test_getrandom_valid_args: ");
    if (test_getrandom_valid_args()) {
        printf("PASS\n");
    } else {
        printf("FAIL\n");
        passed = false;
    }

    printf("test_getrandom_error_passthrough: ");
    if (test_getrandom_error_passthrough()) {
        printf("PASS\n");
    } else {
        printf("FAIL\n");
        passed = false;
    }

    return passed ? 0 : 1;
}