#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

// Prototypes for the renamed functions
// We will compile libgen.c with -Dbasename=tested_basename -Ddirname=tested_dirname
char *tested_basename(char *path);
char *tested_dirname(char *path);

struct test_case {
    const char *path;
    const char *expected;
};

// Test cases from POSIX examples and edge cases
struct test_case cases[] = {
    { "/usr/lib", "lib" },
    { "/usr/", "usr" },
    { "/", "/" },
    { "///", "/" },
    { "//usr//lib//", "lib" },
    { ".", "." },
    { "..", ".." },
    { "", "." },
    { NULL, "." },
    { "usr", "usr" },
    { "/usr/lib/libm.a", "libm.a" },
    { "foo/bar/baz", "baz" },
    { "/foo/bar/baz/", "baz" },
    { "/usr/bin/sort", "sort" },
    { "include/stdio.h", "stdio.h" },
};

int main(void) {
    int failed = 0;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char *input = NULL;
        if (cases[i].path) {
            input = strdup(cases[i].path); // basename may modify input
        }

        char *res = tested_basename(input);

        if (strcmp(res, cases[i].expected) != 0) {
            printf("FAIL: path='%s' expected='%s' got='%s'\n",
                   cases[i].path ? cases[i].path : "NULL",
                   cases[i].expected, res);
            failed++;
        }

        if (input) free(input);
    }

    if (failed) {
        printf("FAILED %d tests\n", failed);
        return 1;
    }

    printf("All basename unit tests passed.\n");
    return 0;
}
