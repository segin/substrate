#include "test_common.h"

// Define macros to bypass architecture-specific headers during native testing
#define _SYS_SYSCALL_H
#define _UNISTD_H
#include <unistd.h>
#include <sys/stat.h>

// Include the source file directly to test its static functions
#include "../../../usr.lib/link/liblink.c"

int test_ln_basename_const(void) {
    // NULL and empty strings
    // According to liblink.c: if (!path || *path == '\0') return path ? path : "";
    TEST_ASSERT_STR(ln_basename_const(NULL), "");
    TEST_ASSERT_STR(ln_basename_const(""), "");

    // Single and multiple root slashes
    TEST_ASSERT_STR(ln_basename_const("/"), "/");
    TEST_ASSERT_STR(ln_basename_const("//"), "//");
    TEST_ASSERT_STR(ln_basename_const("///"), "///");

    // Paths without slashes
    TEST_ASSERT_STR(ln_basename_const("usr"), "usr");
    TEST_ASSERT_STR(ln_basename_const("liblink.c"), "liblink.c");

    // Paths with trailing slashes
    TEST_ASSERT_STR(ln_basename_const("usr/"), "usr/");
    TEST_ASSERT_STR(ln_basename_const("usr/lib/"), "lib/");
    TEST_ASSERT_STR(ln_basename_const("/usr/lib/"), "lib/");
    TEST_ASSERT_STR(ln_basename_const("//usr//lib//"), "lib//");

    // Normal paths
    TEST_ASSERT_STR(ln_basename_const("/usr/lib"), "lib");
    TEST_ASSERT_STR(ln_basename_const("usr/lib"), "lib");
    TEST_ASSERT_STR(ln_basename_const("/usr/lib/liblink.c"), "liblink.c");
    TEST_ASSERT_STR(ln_basename_const("usr/lib/liblink.c"), "liblink.c");

    // Paths with multiple internal slashes
    TEST_ASSERT_STR(ln_basename_const("/usr//lib"), "lib");
    TEST_ASSERT_STR(ln_basename_const("/usr///lib"), "lib");
    TEST_ASSERT_STR(ln_basename_const("//usr//lib"), "lib");

    // Dot and dot-dot
    TEST_ASSERT_STR(ln_basename_const("."), ".");
    TEST_ASSERT_STR(ln_basename_const(".."), "..");
    TEST_ASSERT_STR(ln_basename_const("./"), "./");
    TEST_ASSERT_STR(ln_basename_const("../"), "../");
    TEST_ASSERT_STR(ln_basename_const("/usr/lib/."), ".");
    TEST_ASSERT_STR(ln_basename_const("/usr/lib/.."), "..");

    return 0;
}
