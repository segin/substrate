#include <kern/console.h>
#include <stddef.h>
#include "tests.h"

extern int sys_unlink(const char *path);
extern int sys_open(const char *path, int flags, int mode);
extern int sys_close(int fd);
extern int sys_write(int fd, const void *buf, size_t count);
extern int sys_mkdir(const char *path, int mode);
extern int sys_rmdir(const char *path);
extern int sys_access(const char *path, int mode);

#define O_WRONLY 1
#define O_CREAT 64
#define F_OK 0

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

    // Test 4: Functional Test - Create and Unlink File
    const char *test_file = "/unlink_test_file";
    int fd = sys_open(test_file, O_WRONLY | O_CREAT, 0666);
    if (fd >= 0) {
        sys_write(fd, "test", 4);
        sys_close(fd);

        // Verify it exists
        if (sys_access(test_file, F_OK) == 0) {
            // Now unlink it
            if (sys_unlink(test_file) == 0) {
                // Verify it is gone
                if (sys_access(test_file, F_OK) != 0) {
                    kprint("PASS: Successfully unlinked created file\n");
                } else {
                    kprint("FAIL: File still exists after unlink\n");
                }
            } else {
                kprint("FAIL: sys_unlink returned error for existing file\n");
            }
        } else {
            kprint("FAIL: Could not verify created file existence (access failed)\n");
        }
    } else {
        kprint("FAIL: Could not create test file for unlink test\n");
    }

    // Test 5: Unlink Directory (Should Fail)
    const char *test_dir = "/unlink_test_dir";
    if (sys_mkdir(test_dir, 0777) == 0) {
        if (sys_unlink(test_dir) == -1) {
            kprint("PASS: sys_unlink on directory returned -1 (expected)\n");
        } else {
            kprint("FAIL: sys_unlink on directory succeeded (unexpected)\n");
        }
        sys_rmdir(test_dir); // Cleanup
    } else {
        kprint("FAIL: Could not create test directory\n");
    }
}
