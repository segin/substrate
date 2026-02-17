/*
 * Unit tests for String Library Functions
 */

#include <kern/console.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "tests.h"

extern int snprintf(char *str, size_t size, const char *format, ...);

static int failed_tests = 0;

static void fail(const char *msg) {
    kprint("FAIL: ");
    kprint(msg);
    kprint("\n");
    failed_tests++;
}

#define ASSERT_EQ(a, b, msg) do { \
    if ((uintptr_t)(a) != (uintptr_t)(b)) { \
        fail(msg); \
    } \
} while(0)

static void test_strpbrk(void) {
    const char *s = "hello world";
    char *res;

    // Basic match: 'e' is in "abcde"
    res = strpbrk(s, "abcde");
    ASSERT_EQ(res, s + 1, "strpbrk basic match 'e'");

    // Basic match: 'o' is in "wor"
    res = strpbrk(s, "wor");
    ASSERT_EQ(res, s + 4, "strpbrk basic match 'o'");

    // No match
    res = strpbrk(s, "xyz");
    ASSERT_EQ(res, NULL, "strpbrk no match");

    // Match at beginning
    res = strpbrk(s, "h");
    ASSERT_EQ(res, s, "strpbrk match start");

    // Match at end
    res = strpbrk(s, "d");
    ASSERT_EQ(res, s + 10, "strpbrk match end");

    // Empty s1
    res = strpbrk("", "abc");
    ASSERT_EQ(res, NULL, "strpbrk empty s1");

    // Empty s2
    res = strpbrk(s, "");
    ASSERT_EQ(res, NULL, "strpbrk empty s2");

    // Multiple matches (should return first occurrence in s1)
    // s = "hello world"
    // accept = "lo" -> first 'l' at index 2
    res = strpbrk(s, "lo");
    ASSERT_EQ(res, s + 2, "strpbrk multiple matches");

    // Accept string with characters not in source
    res = strpbrk("abc", "z");
    ASSERT_EQ(res, NULL, "strpbrk not in source");

    // Accept string is substring of source
    // "hello", accept "el" -> first match 'e' at index 1
    const char *s_subset = "hello";
    res = strpbrk(s_subset, "el");
    ASSERT_EQ(res, s_subset + 1, "strpbrk accept subset");

    // Source contains duplicates, accept matches one
    // "banana", accept "n" -> first 'n' at index 2
    const char *s_banana = "banana";
    res = strpbrk(s_banana, "n");
    ASSERT_EQ(res, s_banana + 2, "strpbrk source dups");

    // Accept contains duplicates
    // "hello", accept "ll" -> matches first 'l' at index 2
    const char *s_hello = "hello";
    res = strpbrk(s_hello, "ll");
    ASSERT_EQ(res, s_hello + 2, "strpbrk accept dups");

    // Long string test
    char long_str[100];
    memset(long_str, 'a', 99);
    long_str[99] = '\0';
    long_str[50] = 'b';
    res = strpbrk(long_str, "b");
    ASSERT_EQ(res, long_str + 50, "strpbrk long string");
}

static void test_strchr_basic(void) {
    char buf[] = "Hello World";

    // Found 'W'
    char *res = strchr(buf, 'W');
    if (res != buf + 6) {
        char msg[128];
        snprintf(msg, sizeof(msg), "FAIL: strchr('W') expected %p, got %p\n", (void*)(buf + 6), (void*)res);
        kprint(msg);
        failed_tests++;
    }

    // Not found 'z'
    res = strchr(buf, 'z');
    if (res != NULL) {
        char msg[128];
        snprintf(msg, sizeof(msg), "FAIL: strchr('z') expected NULL, got %p\n", (void*)res);
        kprint(msg);
        failed_tests++;
    }

    // Found terminator
    res = strchr(buf, '\0');
    if (res != buf + 11) {
        char msg[128];
        snprintf(msg, sizeof(msg), "FAIL: strchr('\\0') expected %p, got %p\n", (void*)(buf + 11), (void*)res);
        kprint(msg);
        failed_tests++;
    }
}

static void test_strchr_empty(void) {
    char buf[] = "";
    char *res = strchr(buf, '\0');
    if (res != buf) {
        char msg[128];
        snprintf(msg, sizeof(msg), "FAIL: strchr(\"\", '\\0') expected %p, got %p\n", (void*)buf, (void*)res);
        kprint(msg);
        failed_tests++;
    }

    res = strchr(buf, 'a');
    if (res != NULL) {
        char msg[128];
        snprintf(msg, sizeof(msg), "FAIL: strchr(\"\", 'a') expected NULL, got %p\n", (void*)res);
        kprint(msg);
        failed_tests++;
    }
}

void run_string_tests(void) {
    kprint("\n=== STRING TESTS ===\n");
    failed_tests = 0;

    test_strpbrk();
    test_strchr_basic();
    test_strchr_empty();

    if (failed_tests == 0) {
        kprint("String Tests: PASS\n");
    } else {
        kprint("String Tests: FAIL\n");
    }
    kprint("=== STRING TESTS COMPLETE ===\n\n");
}
