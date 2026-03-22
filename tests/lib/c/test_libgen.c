#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// Forward declarations for renamed functions
char *libc___xpg_basename(char *path);
char *libc_dirname(char *path);

// Redefine libc_basename to call the XPG version
#define libc_basename libc___xpg_basename

// Helper macros for testing
#define ASSERT_STREQ(actual, expected, msg) do { \
    const char *act = (actual); \
    const char *exp = (expected); \
    if (act == NULL && exp == NULL) { \
        /* both NULL is OK */ \
    } else if (act == NULL || exp == NULL) { \
        fprintf(stderr, "FAIL: %s: expected '%s', got '%s'\n", msg, exp ? exp : "NULL", act ? act : "NULL"); \
        exit(1); \
    } else if (strcmp(act, exp) != 0) { \
        fprintf(stderr, "FAIL: %s: expected '%s', got '%s'\n", msg, exp, act); \
        exit(1); \
    } \
} while(0)

struct test_case {
    const char *input;
    const char *expected_basename;
    const char *expected_dirname;
};

static struct test_case test_cases[] = {
    // path, expected_basename, expected_dirname
    { "/usr/lib", "lib", "/usr" },
    { "/usr/", "usr", "/" },
    { "/", "/", "/" },
    { "///", "/", "/" },
    { "//usr//lib//", "lib", "//usr" },
    { ".", ".", "." },
    { "..", "..", "." },
    { "", ".", "." },
    { NULL, ".", "." },
    { "usr", "usr", "." },
    { "/usr/lib/libm.a", "libm.a", "/usr/lib" },
    { "foo/bar/baz", "baz", "foo/bar" },
    { "/foo/bar/baz/", "baz", "/foo/bar" },
    { "/usr/bin/sort", "sort", "/usr/bin" },
    { "include/stdio.h", "stdio.h", "include" },
    { "/usr", "usr", "/" },
    { "///usr", "usr", "/" },
    { "usr/", "usr", "." },
    { "/etc/passwd", "passwd", "/etc" },
};

void run_basename_tests(void) {
    printf("Running basename tests...\n");
    for (size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); i++) {
        char *input = test_cases[i].input ? strdup(test_cases[i].input) : NULL;
        char *res = libc_basename(input);
        char msg[256];
        snprintf(msg, sizeof(msg), "basename(\"%s\")", test_cases[i].input ? test_cases[i].input : "NULL");
        ASSERT_STREQ(res, test_cases[i].expected_basename, msg);
        if (input) free(input);
    }
}

void run_dirname_tests(void) {
    printf("Running dirname tests...\n");
    for (size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); i++) {
        char *input = test_cases[i].input ? strdup(test_cases[i].input) : NULL;
        char *res = libc_dirname(input);
        char msg[256];
        snprintf(msg, sizeof(msg), "dirname(\"%s\")", test_cases[i].input ? test_cases[i].input : "NULL");
        ASSERT_STREQ(res, test_cases[i].expected_dirname, msg);
        if (input) free(input);
    }
}

int main(void) {
    run_basename_tests();
    run_dirname_tests();
    printf("All libgen tests passed!\n");
    return 0;
}
