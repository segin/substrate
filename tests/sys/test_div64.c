/*
 * Unit tests for 64-bit division helpers (sys/lib/div64.c)
 */

#include <kern/console.h>
#include <stdint.h>
#include <string.h>
#include "tests.h"

// External declaration for snprintf if not in a header
extern int snprintf(char *str, size_t size, const char *format, ...);

// External declarations for div64 functions to ensure we are testing the library
extern uint64_t __udivdi3(uint64_t n, uint64_t d);
extern uint64_t __umoddi3(uint64_t n, uint64_t d);
extern int64_t __divdi3(int64_t a, int64_t b);
extern int64_t __moddi3(int64_t a, int64_t b);
extern uint64_t __ashldi3(uint64_t a, int b);
extern uint64_t __lshrdi3(uint64_t a, int b);
extern int64_t __ashrdi3(int64_t a, int b);
extern int64_t __muldi3(int64_t a, int64_t b);
extern int64_t __negdi2(int64_t a);

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

static void test_explicit_calls(void) {
    // Call functions directly to ensure symbols are available and correct
    assert_eq_u64(__udivdi3(100, 10), 10, "__udivdi3(100, 10)");
    assert_eq_u64(__umoddi3(100, 3), 1, "__umoddi3(100, 3)");
    assert_eq_i64(__divdi3(-100, 10), -10, "__divdi3(-100, 10)");
    assert_eq_i64(__moddi3(-10, 3), -1, "__moddi3(-10, 3)");
    assert_eq_u64(__ashldi3(1, 1), 2, "__ashldi3(1, 1)");
    assert_eq_u64(__lshrdi3(2, 1), 1, "__lshrdi3(2, 1)");
    assert_eq_i64(__ashrdi3(-2, 1), -1, "__ashrdi3(-2, 1)");
    assert_eq_i64(__muldi3(10, 10), 100, "__muldi3(10, 10)");
    assert_eq_i64(__negdi2(10), -10, "__negdi2(10)");
}

static void test_shifts(void) {
    uint64_t u = 1;
    int64_t s = -2;

    // <<
    assert_eq_u64(u << 1, 2, "1 << 1");
    assert_eq_u64(u << 32, 4294967296ULL, "1 << 32");

    // >> (logical)
    u = 2;
    assert_eq_u64(u >> 1, 1, "2 >> 1");
    u = 0x8000000000000000ULL;
    assert_eq_u64(u >> 1, 0x4000000000000000ULL, "High bit >> 1");

    // >> (arithmetic)
    assert_eq_i64(s >> 1, -1, "-2 >> 1");
    s = -4;
    assert_eq_i64(s >> 1, -2, "-4 >> 1");
}

static void test_mul(void) {
    uint64_t u = 10;
    int64_t s = -10;

    assert_eq_u64(u * 10, 100, "10 * 10");
    assert_eq_i64(s * 10, -100, "-10 * 10");
    assert_eq_i64(s * -10, 100, "-10 * -10");

    // Overflow
    uint64_t big = 0x100000000ULL;
    assert_eq_u64(big * big, 0, "2^32 * 2^32 = 0 (low 64)");
}

static void test_neg(void) {
    int64_t s = 10;
    assert_eq_i64(-s, -10, "-10");
    s = -10;
    assert_eq_i64(-s, 10, "--10");
    s = 0;
    assert_eq_i64(-s, 0, "-0");
}

void run_div64_tests(void) {
    kprint("\n=== DIV64 TESTS ===\n");
    failed_tests = 0;

    test_unsigned_div();
    test_unsigned_mod();
    test_signed_div();
    test_signed_mod();

    test_explicit_calls();
    test_shifts();
    test_mul();
    test_neg();

    if (failed_tests == 0) {
        kprint("DIV64: PASS\n");
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "DIV64: FAIL (%d failures)\n", failed_tests);
        kprint(buf);
    }
    kprint("=== DIV64 TESTS COMPLETE ===\n\n");
}
