#include "../kern/console.h"
#include <stddef.h>
#include "tests.h"

extern int sys_link(const char *old, const char *new);

void run_link_tests(void) {
    kprint("TEST: Checking sys_link...\n");
    
    // Test 1: NULL paths
    if (sys_link(NULL, "/tmp/new") == -1) {
        kprint("PASS: sys_link(NULL, ...) returns -1\n");
    } else {
        kprint("FAIL: sys_link(NULL, ...) did not return -1\n");
    }
    
    if (sys_link("/tmp/old", NULL) == -1) {
        kprint("PASS: sys_link(..., NULL) returns -1\n");
    } else {
        kprint("FAIL: sys_link(..., NULL) did not return -1\n");
    }
    
    // Test 2: Non-existent old path
    if (sys_link("/this/file/does/not/exist", "/tmp/link") == -1) {
        kprint("PASS: sys_link(non_existent) returns -1\n");
    } else {
        kprint("FAIL: sys_link(non_existent) did not return -1\n");
    }
}
