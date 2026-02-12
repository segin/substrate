/*
 * Unit tests for 64-bit division helpers (sys/lib/div64.c)
 */

#include <kern/console.h>
#include <stdint.h>
#include <string.h>
#include "tests.h"

// External declaration for snprintf if not in a header
extern int snprintf(char *str, size_t size, const char *format, ...);

static int failed_tests = 0;

static void assert_eq_u64(uint64_t actual, uint64_t expected, const char *msg) {
    if (actual != expected) {
        char buf[256];
        snprintf(buf, sizeof(buf), "FAIL: %s (expected %llu, got %llu)\n", msg, expected, actual);
        kprint(buf);
        failed_tests++;
    }
}

static void assert_eq_i64(int64_t actual, int64_t expected, const char *msg) {
    if (actual != expected) {
        char buf[256];
        snprintf(buf, sizeof(buf), "FAIL: %s (expected %lld, got %lld)\n", msg, expected, actual);
        kprint(buf);
        failed_tests++;
    }
}

static void test_unsigned_div(void) {
    uint64_t a, b, q;

    a = 100; b = 10;
    q = a / b;
    assert_eq_u64(q, 10, "100 / 10");

    a = 100; b = 3;
    q = a / b;
    assert_eq_u64(q, 33, "100 / 3");

    a = 123456789012345ULL; b = 100000000ULL;
    q = a / b;
    assert_eq_u64(q, 1234567, "Large / Medium");

    // Division by 1
    a = 0xFFFFFFFFFFFFFFFFULL; b = 1;
    q = a / b;
    assert_eq_u64(q, 0xFFFFFFFFFFFFFFFFULL, "Max / 1");

    // Division of 0
    a = 0; b = 12345;
    q = a / b;
    assert_eq_u64(q, 0, "0 / 12345");
}

static void test_unsigned_mod(void) {
    uint64_t a, b, r;

    a = 100; b = 10;
    r = a % b;
    assert_eq_u64(r, 0, "100 % 10");

    a = 100; b = 3;
    r = a % b;
    assert_eq_u64(r, 1, "100 % 3");

    a = 0xFFFFFFFFFFFFFFFFULL; b = 2;
    r = a % b;
    assert_eq_u64(r, 1, "Max % 2");
}

static void test_signed_div(void) {
    int64_t a, b, q;

    a = 100; b = 10;
    q = a / b;
    assert_eq_i64(q, 10, "100 / 10");

    a = 100; b = -10;
    q = a / b;
    assert_eq_i64(q, -10, "100 / -10");

    a = -100; b = 10;
    q = a / b;
    assert_eq_i64(q, -10, "-100 / 10");

    a = -100; b = -10;
    q = a / b;
    assert_eq_i64(q, 10, "-100 / -10");

    // Truncation towards zero
    a = 100; b = -3;
    q = a / b;
    assert_eq_i64(q, -33, "100 / -3");
}

static void test_signed_mod(void) {
    int64_t a, b, r;

    a = 10; b = 3;
    r = a % b;
    assert_eq_i64(r, 1, "10 % 3");

    a = 10; b = -3;
    r = a % b;
    assert_eq_i64(r, 1, "10 % -3");

    a = -10; b = 3;
    r = a % b;
    assert_eq_i64(r, -1, "-10 % 3");

    a = -10; b = -3;
    r = a % b;
    assert_eq_i64(r, -1, "-10 % -3");
}

void run_div64_tests(void) {
    kprint("\n=== DIV64 TESTS ===\n");
    failed_tests = 0;

    test_unsigned_div();
    test_unsigned_mod();
    test_signed_div();
    test_signed_mod();

    if (failed_tests == 0) {
        kprint("DIV64: PASS\n");
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "DIV64: FAIL (%d failures)\n", failed_tests);
        kprint(buf);
    }
    kprint("=== DIV64 TESTS COMPLETE ===\n\n");
}
