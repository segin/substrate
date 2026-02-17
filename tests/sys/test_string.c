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
}

static void test_strcmp(void) {
    // Basic equality
    ASSERT_EQ(strcmp("", ""), 0, "strcmp empty");
    ASSERT_EQ(strcmp("abc", "abc"), 0, "strcmp equal");

    // Basic inequality
    if (strcmp("abc", "abd") >= 0) fail("strcmp('abc', 'abd') should be negative");
    if (strcmp("abd", "abc") <= 0) fail("strcmp('abd', 'abc') should be positive");

    // Prefix handling
    if (strcmp("abc", "abcd") >= 0) fail("strcmp prefix ('abc', 'abcd') should be negative");
    if (strcmp("abcd", "abc") <= 0) fail("strcmp prefix ('abcd', 'abc') should be positive");

    // Empty vs Non-empty
    if (strcmp("", "a") >= 0) fail("strcmp empty vs 'a' should be negative");
    if (strcmp("a", "") <= 0) fail("strcmp 'a' vs empty should be positive");

    // Unsigned char comparison (High bit set)
    // '\xff' is 255 (unsigned), so it should be greater than '\x01' (1)
    if (strcmp("\xff", "\x01") <= 0) fail("strcmp unsigned comparison ('\\xff', '\\x01') should be positive");
    if (strcmp("\x01", "\xff") >= 0) fail("strcmp unsigned comparison ('\\x01', '\\xff') should be negative");
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
    test_strcmp();

    if (failed_tests == 0) {
        kprint("String Tests: PASS\n");
    } else {
        kprint("String Tests: FAIL\n");
    }
    kprint("=== STRING TESTS COMPLETE ===\n\n");
}
