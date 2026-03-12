#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
#include <sys/types.h>

#ifndef HOST_TEST
#define HOST_TEST
#endif

/* Manually define syscall numbers since we can't easily include them without target architecture */
#ifndef SYS_GETUID
#define SYS_GETUID 24
#endif
#ifndef SYS_GETGID
#define SYS_GETGID 47
#endif
#ifndef SYS_GETEUID
#define SYS_GETEUID 49
#endif
#ifndef SYS_GETEGID
#define SYS_GETEGID 50
#endif
#ifndef SYS_SETUID
#define SYS_SETUID 23
#endif
#ifndef SYS_SETGID
#define SYS_SETGID 46
#endif

long mock_syscall_number = -1;
long mock_syscall_arg1 = -1;
long mock_syscall_return = 0;

long syscall(long number, ...) {
    mock_syscall_number = number;
    va_list args;
    va_start(args, number);

    /* Only extract argument if the syscall takes one */
    if (number == SYS_SETUID || number == SYS_SETGID) {
        mock_syscall_arg1 = va_arg(args, long);
    }
    va_end(args);

    return mock_syscall_return;
}

#include "../../../lib/sys/getuid.c"

void test_getuid(void) {
    mock_syscall_return = 1000;
    mock_syscall_number = -1;
    uid_t uid = getuid();
    assert(mock_syscall_number == SYS_GETUID);
    assert(uid == 1000);
    printf("PASS: getuid()\n");
}

void test_getgid(void) {
    mock_syscall_return = 2000;
    mock_syscall_number = -1;
    gid_t gid = getgid();
    assert(mock_syscall_number == SYS_GETGID);
    assert(gid == 2000);
    printf("PASS: getgid()\n");
}

void test_geteuid(void) {
    mock_syscall_return = 3000;
    mock_syscall_number = -1;
    uid_t euid = geteuid();
    assert(mock_syscall_number == SYS_GETEUID);
    assert(euid == 3000);
    printf("PASS: geteuid()\n");
}

void test_getegid(void) {
    mock_syscall_return = 4000;
    mock_syscall_number = -1;
    gid_t egid = getegid();
    assert(mock_syscall_number == SYS_GETEGID);
    assert(egid == 4000);
    printf("PASS: getegid()\n");
}

void test_setuid(void) {
    mock_syscall_return = 0;
    mock_syscall_number = -1;
    mock_syscall_arg1 = -1;
    int res = setuid(1001);
    assert(mock_syscall_number == SYS_SETUID);
    assert(mock_syscall_arg1 == 1001);
    assert(res == 0);
    printf("PASS: setuid()\n");

    mock_syscall_return = -1;
    mock_syscall_number = -1;
    mock_syscall_arg1 = -1;
    res = setuid(1002);
    assert(mock_syscall_number == SYS_SETUID);
    assert(mock_syscall_arg1 == 1002);
    assert(res == -1);
    printf("PASS: setuid() error\n");
}

void test_setgid(void) {
    mock_syscall_return = 0;
    mock_syscall_number = -1;
    mock_syscall_arg1 = -1;
    int res = setgid(2001);
    assert(mock_syscall_number == SYS_SETGID);
    assert(mock_syscall_arg1 == 2001);
    assert(res == 0);
    printf("PASS: setgid()\n");

    mock_syscall_return = -1;
    mock_syscall_number = -1;
    mock_syscall_arg1 = -1;
    res = setgid(2002);
    assert(mock_syscall_number == SYS_SETGID);
    assert(mock_syscall_arg1 == 2002);
    assert(res == -1);
    printf("PASS: setgid() error\n");
}

int main(void) {
    printf("Testing getuid.c syscall wrappers...\n");
    test_getuid();
    test_getgid();
    test_geteuid();
    test_getegid();
    test_setuid();
    test_setgid();
    printf("All getuid/getgid tests passed!\n");
    return 0;
}
