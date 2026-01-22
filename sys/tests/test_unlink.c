#include <kern/console.h>
#include <stddef.h>
#include "tests.h"

extern int sys_unlink(const char *path);

void run_unlink_tests(void) {
    kprint("TEST: Checking sys_unlink...\n");
    
    // Test 1: NULL path
    if (sys_unlink(NULL) == -1) {
        kprint("PASS: sys_unlink(NULL) returns -1\n");
    } else {
        kprint("FAIL: sys_unlink(NULL) did not return -1\n");
    }
    
    // Test 2: Empty path
    if (sys_unlink("") == -1) {
        kprint("PASS: sys_unlink(\"\") returns -1\n");
    } else {
        kprint("FAIL: sys_unlink(\"\") did not return -1\n");
    }
    
    // Test 3: Non-existent path
    if (sys_unlink("/this/file/does/not/exist") == -1) {
        kprint("PASS: sys_unlink(non_existent) returns -1\n");
    } else {
        kprint("FAIL: sys_unlink(non_existent) did not return -1\n");
    }
}
