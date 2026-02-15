#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/*
 * Renamed functions to avoid conflicts with host libc.
 * These will be linked from libgen_prefixed.o
 */
char *libc_basename(char *path);
char *libc_dirname(char *path);

/*
 * Helper function to act as libc_strlen which libgen.o will look for
 * (since it was compiled against a libc that expects libc_strlen
 * after objcopy prefixing).
 */
size_t libc_strlen(const char *s) {
    return strlen(s);
}

/*
 * Helper macro for testing string equality
 */
#define ASSERT_STREQ(actual, expected, msg) do { \
    char *act = (actual); \
    char *exp = (expected); \
    if (strcmp(act, exp) != 0) { \
        fprintf(stderr, "FAIL: %s: expected '%s', got '%s'\n", msg, exp, act); \
        exit(1); \
    } \
} while(0)

void test_basename(void) {
    printf("Running basename tests...\n");

    {
        char path[] = "/usr/lib";
        ASSERT_STREQ(libc_basename(path), "lib", "/usr/lib -> lib");
    }
    {
        char path[] = "/usr/";
        ASSERT_STREQ(libc_basename(path), "usr", "/usr/ -> usr");
    }
    {
        char path[] = "usr";
        ASSERT_STREQ(libc_basename(path), "usr", "usr -> usr");
    }
    {
        char path[] = "/";
        ASSERT_STREQ(libc_basename(path), "/", "/ -> /");
    }
    {
        char path[] = ".";
        ASSERT_STREQ(libc_basename(path), ".", ". -> .");
    }
    {
        char path[] = "..";
        ASSERT_STREQ(libc_basename(path), "..", ".. -> ..");
    }
    {
        char path[] = "///";
        ASSERT_STREQ(libc_basename(path), "/", "/// -> /");
    }
    {
        char path[] = "";
        ASSERT_STREQ(libc_basename(path), ".", "empty -> .");
    }
    {
        ASSERT_STREQ(libc_basename(NULL), ".", "NULL -> .");
    }
    {
        char path[] = "foo/bar";
        ASSERT_STREQ(libc_basename(path), "bar", "foo/bar -> bar");
    }
    {
        char path[] = "/usr/lib//";
        ASSERT_STREQ(libc_basename(path), "lib", "/usr/lib// -> lib");
    }

    printf("basename tests passed!\n");
}

void test_dirname(void) {
    printf("Running dirname tests...\n");

    {
        char path[] = "/usr/lib";
        ASSERT_STREQ(libc_dirname(path), "/usr", "/usr/lib -> /usr");
    }
    {
        char path[] = "/usr/";
        ASSERT_STREQ(libc_dirname(path), "/", "/usr/ -> /");
    }
    {
        char path[] = "usr";
        ASSERT_STREQ(libc_dirname(path), ".", "usr -> .");
    }
    {
        char path[] = "/";
        ASSERT_STREQ(libc_dirname(path), "/", "/ -> /");
    }
    {
        char path[] = ".";
        ASSERT_STREQ(libc_dirname(path), ".", ". -> .");
    }
    {
        char path[] = "..";
        ASSERT_STREQ(libc_dirname(path), ".", ".. -> .");
    }
    {
        char path[] = "///";
        ASSERT_STREQ(libc_dirname(path), "/", "/// -> /");
    }
    {
        char path[] = "";
        ASSERT_STREQ(libc_dirname(path), ".", "empty -> .");
    }
    {
        ASSERT_STREQ(libc_dirname(NULL), ".", "NULL -> .");
    }
    {
        char path[] = "foo/bar";
        ASSERT_STREQ(libc_dirname(path), "foo", "foo/bar -> foo");
    }
    {
        char path[] = "/foo/bar";
        ASSERT_STREQ(libc_dirname(path), "/foo", "/foo/bar -> /foo");
    }
    {
        char path[] = "/usr/lib//";
        ASSERT_STREQ(libc_dirname(path), "/usr", "/usr/lib// -> /usr");
    }

    printf("dirname tests passed!\n");
}

int main(void) {
    test_basename();
    test_dirname();
    return 0;
}
