#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <sys/types.h>

// Prevent including conflicting kernel headers when compiling on host
#define _SYS_SYSCALL_H

// Mock syscall numbers
#define SYS_PROC_INFO 1
#define SYS_PROC_LIST 2

// Mock syscall function
long syscall(long number, ...);
long syscall(long number, ...) {
    (void)number;
    return 0;
}

// Rename main to avoid conflict, and include the source file to test
#define main ps_main
#include "../../../bin/ps/ps.c"
#undef main

void test_state_to_char(void) {
    // Valid states mapping
    assert(strcmp(state_to_char(1), "IDL") == 0);
    assert(strcmp(state_to_char(2), "RUN") == 0);
    assert(strcmp(state_to_char(3), "SLP") == 0);
    assert(strcmp(state_to_char(4), "STP") == 0);
    assert(strcmp(state_to_char(5), "ZOM") == 0);
    assert(strcmp(state_to_char(6), "DIE") == 0);

    // Invalid/Unknown states mapping
    assert(strcmp(state_to_char(0), "???") == 0);
    assert(strcmp(state_to_char(7), "???") == 0);
    assert(strcmp(state_to_char(255), "???") == 0);

    printf("All ps state_to_char unit tests passed.\n");
}

int main(void) {
    test_state_to_char();
    return 0;
}
