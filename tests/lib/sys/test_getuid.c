#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdarg.h>

#ifndef HOST_TEST
#define HOST_TEST
#endif

// Mock syscall numbers
#define SYS_GETUID  24
#define SYS_GETEUID 49
#define SYS_GETGID  47
#define SYS_GETEGID 50
#define SYS_SETUID  23
#define SYS_SETGID  46

// Mock syscall
long mock_syscall(long number, ...) {
    va_list args;
    va_start(args, number);
    long arg1 = 0;

    // For setuid/setgid, we extract the first argument
    if (number == SYS_SETUID || number == SYS_SETGID) {
        arg1 = va_arg(args, long);
    }
    va_end(args);

    if (number == SYS_GETUID) {
        return 1000;
    }
    if (number == SYS_GETGID) {
        return 1001;
    }
    if (number == SYS_GETEUID) {
        return 1002;
    }
    if (number == SYS_GETEGID) {
        return 1003;
    }
    if (number == SYS_SETUID) {
        if (arg1 == 1000) return 0; // Success
        return -1; // Failure for other values
    }
    if (number == SYS_SETGID) {
        if (arg1 == 1001) return 0; // Success
        return -1; // Failure for other values
    }
    return -1;
}

#define syscall mock_syscall

// We must remap the names of the functions defined in lib/sys/getuid.c
// so they don't conflict with the host libc definitions.
#define getuid tested_getuid
#define getgid tested_getgid
#define geteuid tested_geteuid
#define getegid tested_getegid
#define setuid tested_setuid
#define setgid tested_setgid

// Prevent loading real syscall.h which has arch-specific includes
#define _SYS_SYSCALL_H

// Include the source directly
#include "../../../lib/sys/getuid.c"

int main() {
    printf("Running getuid tests...\n");

    // Test getuid
    assert(tested_getuid() == 1000);

    // Test getgid
    assert(tested_getgid() == 1001);

    // Test geteuid
    assert(tested_geteuid() == 1002);

    // Test getegid
    assert(tested_getegid() == 1003);

    // Test setuid
    assert(tested_setuid(1000) == 0);
    assert(tested_setuid(9999) == -1); // Test failure case

    // Test setgid
    assert(tested_setgid(1001) == 0);
    assert(tested_setgid(9999) == -1); // Test failure case

    printf("All getuid tests passed!\n");
    return 0;
}
