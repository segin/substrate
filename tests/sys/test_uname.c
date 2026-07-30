#include <sys/utsname.h>
#include <string.h>
/*
 * #425: this called uname(), the LIBC wrapper, which does not exist inside
 * the kernel -- the kernel-side entry point is sys_uname().  An in-kernel
 * test has to call the kernel function.
 */
#include <sys/syscall_impl.h>
#include <kern/console.h>
#include "tests.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        kprint("FAIL: "); kprint(msg); kprint("\n"); \
        tests_failed++; \
        return; \
    } \
    tests_passed++; \
} while(0)

static void test_uname_basic(void) {
    struct utsname name;
    memset(&name, 0, sizeof(name));
    
    int ret = sys_uname(&name);
    TEST_ASSERT(ret == 0, "uname syscall succeeded");
    
    TEST_ASSERT(strcmp(name.sysname, "Substrate") == 0, "sysname is Substrate");
    TEST_ASSERT(strcmp(name.release, "0.1.0") == 0, "release matches OS_VERSION");
    
    /* version check - strstr Substrate */
    // Note: strstr available in string.h
    extern char *strstr(const char *haystack, const char *needle);
    TEST_ASSERT(strstr(name.version, "Substrate") != NULL, "version contains Substrate string");
    
    TEST_ASSERT(strcmp(name.machine, "i386") == 0, "machine is i386");
    
    // nodename check - just ensure it's null terminated
    TEST_ASSERT(name.nodename[sizeof(name.nodename)-1] == '\0', "nodename is null-terminated");
}

void test_uname(void) {
    kprint("Test: uname kernel interface\n");
    tests_passed = 0;
    tests_failed = 0;

    test_uname_basic();

    if (tests_failed == 0) {
        kprint("  PASS (uname)\n");
    }
}
