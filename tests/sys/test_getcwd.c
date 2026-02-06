#include <kern/console.h>
#include <string.h>
#include <sys/errno.h>

extern int sys_getcwd(char *buf, size_t size);
extern int sys_chdir(const char *path);
extern int sys_mkdir(const char *path, int mode);
extern int sys_rmdir(const char *path);

void run_getcwd_tests(void) {
    kprint("Running getcwd tests...\n");

    char buf[1024];
    int ret;

    // Test 1: Root directory
    // We assume we start at root or can go there.
    sys_chdir("/");
    ret = sys_getcwd(buf, sizeof(buf));
    if (ret != 0) {
        kprint("FAIL: sys_getcwd at root returned error\n");
    } else if (strcmp(buf, "/") != 0) {
        kprint("FAIL: sys_getcwd at root returned: ");
        kprint(buf);
        kprint("\n");
    } else {
        kprint("PASS: sys_getcwd at root\n");
    }

    // Test 2: Create directory /tmp_test_getcwd
    // First try to remove it in case it exists from previous run
    sys_rmdir("/tmp_test_getcwd/nested");
    sys_rmdir("/tmp_test_getcwd");

    ret = sys_mkdir("/tmp_test_getcwd", 0755);
    if (ret != 0 && ret != -17) { // -17 is EEXIST
        kprint("FAIL: sys_mkdir returned error\n");
        return;
    }

    // Test 3: chdir into it
    ret = sys_chdir("/tmp_test_getcwd");
    if (ret != 0) {
        kprint("FAIL: sys_chdir to /tmp_test_getcwd failed\n");
        return;
    }

    // Test 4: getcwd
    ret = sys_getcwd(buf, sizeof(buf));
    if (ret != 0) {
        kprint("FAIL: sys_getcwd returned error\n");
    } else if (strcmp(buf, "/tmp_test_getcwd") != 0) {
        kprint("FAIL: sys_getcwd returned: ");
        kprint(buf);
        kprint(" expected: /tmp_test_getcwd\n");
    } else {
        kprint("PASS: sys_getcwd in subdir\n");
    }

    // Test 5: Nested
    sys_mkdir("nested", 0755);
    sys_chdir("nested");
    ret = sys_getcwd(buf, sizeof(buf));
    if (strcmp(buf, "/tmp_test_getcwd/nested") != 0) {
        kprint("FAIL: sys_getcwd nested returned: ");
        kprint(buf);
        kprint("\n");
    } else {
        kprint("PASS: sys_getcwd nested\n");
    }

    // Test 6: ..
    sys_chdir("..");
    ret = sys_getcwd(buf, sizeof(buf));
    if (strcmp(buf, "/tmp_test_getcwd") != 0) {
        kprint("FAIL: sys_getcwd after .. returned: ");
        kprint(buf);
        kprint("\n");
    } else {
        kprint("PASS: sys_getcwd after ..\n");
    }

    // Cleanup
    // Note: rmdir usually requires empty directory.
    // We are in /tmp_test_getcwd.
    // Remove nested.
    sys_rmdir("nested");

    sys_chdir("/");
    sys_rmdir("/tmp_test_getcwd");

    kprint("getcwd tests complete.\n");
}
