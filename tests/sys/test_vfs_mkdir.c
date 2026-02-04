#include <kern/console.h>
#include <stddef.h>
#include "tests.h"

extern int sys_mkdir(const char *path, int mode);

void run_mkdir_tests(void) {
    kprint("TEST: Checking sys_mkdir...\n");
    
    // Test 1: NULL path
    if (sys_mkdir(NULL, 0) == -1) {
        kprint("PASS: sys_mkdir(NULL, ...) returns -1\n");
    } else {
        kprint("FAIL: sys_mkdir(NULL, ...) did not return -1\n");
    }
    
    // Test 2: Non-existent parent
    if (sys_mkdir("/this/does/not/exist/foo", 0755) == -1) {
        kprint("PASS: sys_mkdir(non_existent_parent) returns -1\n");
    } else {
        kprint("FAIL: sys_mkdir(non_existent_parent) did not return -1\n");
    }

    // Test 3: Relative path (should not crash)
    // Exercises the CWD lookup logic. In this environment CWD might be root (read-only) or NULL.
    // Expecting failure (-1) but checking for safety/crash.
    int ret = sys_mkdir("relative_test_dir", 0755);
    if (ret == -1 || ret == 0) {
        kprint("PASS: sys_mkdir(relative_path) executed safely\n");
    } else {
        kprint("FAIL: sys_mkdir(relative_path) returned unexpected value\n");
    }

    // Test 4: Relative path with subdir
    if (sys_mkdir("subdir/test", 0755) == -1) {
        kprint("PASS: sys_mkdir(relative_subdir) returns -1 (expected in read-only/empty env)\n");
    } else {
        kprint("FAIL: sys_mkdir(relative_subdir) did not return -1\n");
    }
    
    // Note: Can't easily test success without mounting a writable filesystem
    // or creating a mock one here. For now validation relies on failure modes
    // and manual UDF integration testing.
}
