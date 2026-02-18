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

static void test_strchr_comprehensive(void) {
    char buf[] = "Hello World";

    // Test: Search for a character not present in the string
    ASSERT_EQ(strchr(buf, 'z'), NULL, "strchr comprehensive not found");

    // Test: Search for the null terminator
    ASSERT_EQ(strchr(buf, '\0'), buf + 11, "strchr comprehensive null terminator");

    // Test: Search in an empty string
    char empty[] = "";
    ASSERT_EQ(strchr(empty, 'a'), NULL, "strchr comprehensive empty string not found");
    ASSERT_EQ(strchr(empty, '\0'), empty, "strchr comprehensive empty string null terminator");

    // Test: Verify int c argument conversion (e.g., c values > 255)
    ASSERT_EQ(strchr(buf, 'W' + 256), buf + 6, "strchr comprehensive int conversion > 255");

    // Test: Search for high-bit characters (0x80-0xFF)
    unsigned char high_bit_buf[] = { 0x80, 0xFF, 0x00 };
    ASSERT_EQ(strchr((char *)high_bit_buf, 0x80), high_bit_buf, "strchr comprehensive high bit 0x80");
    ASSERT_EQ(strchr((char *)high_bit_buf, 0xFF), high_bit_buf + 1, "strchr comprehensive high bit 0xFF");

    // Test: Verify function returns the first occurrence
    char multiple[] = "ababa";
    ASSERT_EQ(strchr(multiple, 'a'), multiple, "strchr comprehensive first occurrence 'a'");
    ASSERT_EQ(strchr(multiple, 'b'), multiple + 1, "strchr comprehensive first occurrence 'b'");
}

static void test_memcmp(void) {
    char b1[256], b2[256];
    memset(b1, 0, sizeof(b1));
    memset(b2, 0, sizeof(b2));

    if (memcmp(b1, b2, 256) != 0) {
        fail("memcmp identity failed");
    }

    for (int i = 0; i < 256; i++) {
        b1[i] = (char)i;
        b2[i] = (char)i;
    }

    if (memcmp(b1, b2, 256) != 0) {
        fail("memcmp sequence identity failed");
    }

    for (int i = 0; i < 256; i++) {
        // Test smaller
        if (b1[i] < (char)255) {
            b2[i] = b1[i] + 1;
            if (memcmp(b1, b2, 256) >= 0) {
                char msg[64];
                snprintf(msg, sizeof(msg), "memcmp < failed at %d", i);
                fail(msg);
            }
            b2[i] = b1[i];
        }
        if (b1[i] > 0) {
            b2[i] = b1[i] - 1;
            if (memcmp(b1, b2, 256) <= 0) {
                char msg[64];
                snprintf(msg, sizeof(msg), "memcmp > failed at %d", i);
                fail(msg);
            }
            b2[i] = b1[i];
        }
    }

    // Unsigned comparison check
    unsigned char u1[] = { 0x00 };
    unsigned char u2[] = { 0xFF };
    if (memcmp(u1, u2, 1) >= 0) {
        fail("memcmp unsigned 0x00 vs 0xFF failed");
    }

    u1[0] = 0x7F; u2[0] = 0x80;
    if (memcmp(u1, u2, 1) >= 0) {
        fail("memcmp unsigned 0x7F vs 0x80 failed");
    }
}
void run_string_tests(void) {
    kprint("\n=== STRING TESTS ===\n");
    failed_tests = 0;

    test_strpbrk();
    test_strchr_basic();
    test_strchr_empty();
    test_strchr_comprehensive();
    test_strcmp();
    test_memcmp();

    if (failed_tests == 0) {
        kprint("String Tests: PASS\n");
    } else {
        kprint("String Tests: FAIL\n");
    }
    kprint("=== STRING TESTS COMPLETE ===\n\n");
}
