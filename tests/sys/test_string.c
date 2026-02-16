/*
 * test_string.c - Kernel String Tests
 */

#include <kern/console.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

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

void run_string_tests(void) {
    kprint("\n=== STRING TESTS ===\n");
    failed_tests = 0;

    test_strpbrk();

    if (failed_tests == 0) {
        kprint("string: PASS\n");
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "string: FAIL (%d tests failed)\n", failed_tests);
        kprint(msg);
    }
}
