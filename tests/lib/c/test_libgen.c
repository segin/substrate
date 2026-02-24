#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <libgen.h>

#undef basename
#undef dirname

// Rename functions to avoid conflicts with host libc
#define basename tested_basename
#define dirname tested_dirname

// Include the implementation under test
#include "../../../lib/c/src/libgen.c"

// Helper function to test basename
void check_basename(const char *path, const char *expected) {
    char *input = path ? strdup(path) : NULL;
    char *res = tested_basename(input);

    if (strcmp(res, expected) != 0) {
        fprintf(stderr, "FAIL: basename(\"%s\") -> \"%s\", expected \"%s\"\n",
                path ? path : "NULL", res, expected);
        if (input) free(input);
        exit(1);
    }

    if (input) free(input);
}

// Helper function to test dirname
void check_dirname(const char *path, const char *expected) {
    char *input = path ? strdup(path) : NULL;
    char *res = tested_dirname(input);

    if (strcmp(res, expected) != 0) {
        fprintf(stderr, "FAIL: dirname(\"%s\") -> \"%s\", expected \"%s\"\n",
                path ? path : "NULL", res, expected);
        if (input) free(input);
        exit(1);
    }

    if (input) free(input);
}

int main(void) {
    printf("Running libgen tests...\n");

    // basename tests
    check_basename("/usr/lib", "lib");
    check_basename("/usr/", "usr");
    check_basename("/", "/");
    check_basename(".", ".");
    check_basename("..", "..");
    check_basename("", ".");
    check_basename(NULL, ".");
    check_basename("///", "/");
    check_basename("//usr//lib//", "lib");
    check_basename("usr", "usr");
    check_basename("/usr/lib/file.txt", "file.txt");
    check_basename("/usr/lib/dir/", "dir");
    check_basename("a/b", "b");
    check_basename("a/", "a");

    // dirname tests
    check_dirname("/usr/lib", "/usr");
    check_dirname("/usr/", "/");
    check_dirname("usr", ".");
    check_dirname("/", "/");
    check_dirname(".", ".");
    check_dirname("..", ".");
    check_dirname("", ".");
    check_dirname(NULL, ".");
    check_dirname("///", "/");
    // Implementation specific: //usr//lib// -> //usr
    check_dirname("//usr//lib//", "//usr");
    check_dirname("/usr/lib/file.txt", "/usr/lib");
    check_dirname("/usr/lib/dir/", "/usr/lib");
    check_dirname("a/b", "a");
    check_dirname("a/", ".");

    printf("All libgen tests passed!\n");
    return 0;
}
