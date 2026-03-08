#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <sys/types.h>

#include "../../../sys/arch/i386/syscall.h"

long mock_syscall_called = 0;
long mock_syscall_num = 0;
long mock_syscall_arg1 = 0;
long mock_syscall_ret = 0;

// Need to match the expected signature in getuid.c
long syscall(long number, ...) {
    mock_syscall_called++;
    mock_syscall_num = number;

    va_list args;
    va_start(args, number);

    if (number == SYS_SETUID || number == SYS_SETGID) {
        mock_syscall_arg1 = va_arg(args, long);
    }

    va_end(args);
    return mock_syscall_ret;
}

// Include the source file directly
#include "../../../lib/sys/getuid.c"

void test_getuid(void) {
    printf("Testing getuid...\n");
    mock_syscall_called = 0;
    mock_syscall_ret = 1000;

    uid_t res = getuid();

    assert(mock_syscall_called == 1);
    assert(mock_syscall_num == SYS_GETUID);
    assert(res == 1000);
    printf("PASS: getuid()\n");
}

void test_getgid(void) {
    printf("Testing getgid...\n");
    mock_syscall_called = 0;
    mock_syscall_ret = 1001;

    gid_t res = getgid();

    assert(mock_syscall_called == 1);
    assert(mock_syscall_num == SYS_GETGID);
    assert(res == 1001);
    printf("PASS: getgid()\n");
}

void test_geteuid(void) {
    printf("Testing geteuid...\n");
    mock_syscall_called = 0;
    mock_syscall_ret = 1002;

    uid_t res = geteuid();

    assert(mock_syscall_called == 1);
    assert(mock_syscall_num == SYS_GETEUID);
    assert(res == 1002);
    printf("PASS: geteuid()\n");
}

void test_getegid(void) {
    printf("Testing getegid...\n");
    mock_syscall_called = 0;
    mock_syscall_ret = 1003;

    gid_t res = getegid();

    assert(mock_syscall_called == 1);
    assert(mock_syscall_num == SYS_GETEGID);
    assert(res == 1003);
    printf("PASS: getegid()\n");
}

void test_setuid(void) {
    printf("Testing setuid...\n");
    mock_syscall_called = 0;
    mock_syscall_ret = 0;

    int res = setuid(2000);

    assert(mock_syscall_called == 1);
    assert(mock_syscall_num == SYS_SETUID);
    assert(mock_syscall_arg1 == 2000);
    assert(res == 0);

    // Test failure
    mock_syscall_called = 0;
    mock_syscall_ret = -1;
    res = setuid(2000);
    assert(mock_syscall_called == 1);
    assert(res == -1);

    printf("PASS: setuid()\n");
}

void test_setgid(void) {
    printf("Testing setgid...\n");
    mock_syscall_called = 0;
    mock_syscall_ret = 0;

    int res = setgid(2001);

    assert(mock_syscall_called == 1);
    assert(mock_syscall_num == SYS_SETGID);
    assert(mock_syscall_arg1 == 2001);
    assert(res == 0);

    // Test failure
    mock_syscall_called = 0;
    mock_syscall_ret = -1;
    res = setgid(2001);
    assert(mock_syscall_called == 1);
    assert(res == -1);

    printf("PASS: setgid()\n");
}

int main(void) {
    printf("Running getuid/getgid syscall wrapper tests...\n");
    test_getuid();
    test_getgid();
    test_geteuid();
    test_getegid();
    test_setuid();
    test_setgid();
    printf("All tests passed!\n");
    return 0;
}
