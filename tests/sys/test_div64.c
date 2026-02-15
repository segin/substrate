/*
 * Unit tests for 64-bit division helpers (sys/lib/div64.c)
 */

#include <kern/console.h>
#include <stdint.h>
#include <string.h>
#include "tests.h"

// External declaration for snprintf if not in a header
extern int snprintf(char *str, size_t size, const char *format, ...);

// Helper function declarations
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

static void test_shifts(void) {
    // Logical right shift (__lshrdi3)
    assert_eq_u64(__lshrdi3(0xFFFFFFFFFFFFFFFFULL, 0), 0xFFFFFFFFFFFFFFFFULL, "lshr 0");
    assert_eq_u64(__lshrdi3(0xFFFFFFFFFFFFFFFFULL, 1), 0x7FFFFFFFFFFFFFFFULL, "lshr 1");
    assert_eq_u64(__lshrdi3(0xFFFFFFFFFFFFFFFFULL, 32), 0x00000000FFFFFFFFULL, "lshr 32");
    assert_eq_u64(__lshrdi3(0xFFFFFFFFFFFFFFFFULL, 63), 1ULL, "lshr 63");
    assert_eq_u64(__lshrdi3(0xFFFFFFFFFFFFFFFFULL, 64), 0xFFFFFFFFFFFFFFFFULL, "lshr 64 (mask)");

    // Left shift (__ashldi3)
    assert_eq_u64(__ashldi3(1ULL, 0), 1ULL, "ashl 0");
    assert_eq_u64(__ashldi3(1ULL, 1), 2ULL, "ashl 1");
    assert_eq_u64(__ashldi3(1ULL, 32), 0x100000000ULL, "ashl 32");
    assert_eq_u64(__ashldi3(1ULL, 63), 0x8000000000000000ULL, "ashl 63");
    assert_eq_u64(__ashldi3(1ULL, 64), 1ULL, "ashl 64 (mask)");

    // Arithmetic right shift (__ashrdi3)
    assert_eq_i64(__ashrdi3(0x7FFFFFFFFFFFFFFFULL, 1), 0x3FFFFFFFFFFFFFFFULL, "ashr pos 1");
    assert_eq_i64(__ashrdi3(0x8000000000000000LL, 1), 0xC000000000000000LL, "ashr neg 1");
    assert_eq_i64(__ashrdi3(-1LL, 1), -1LL, "ashr -1 1");
    assert_eq_i64(__ashrdi3(0x8000000000000000LL, 63), -1LL, "ashr neg 63");
}

static void test_mul(void) {
    assert_eq_i64(__muldi3(10, 10), 100, "10 * 10");
    assert_eq_i64(__muldi3(-10, 10), -100, "-10 * 10");
    assert_eq_i64(__muldi3(10, -10), -100, "10 * -10");
    assert_eq_i64(__muldi3(-10, -10), 100, "-10 * -10");
    assert_eq_i64(__muldi3(0x100000000LL, 2), 0x200000000LL, "Large * 2");
    assert_eq_i64(__muldi3(0xFFFFFFFFLL, 0xFFFFFFFFLL), 0xFFFFFFFE00000001LL, "Large * Large");
}

static void test_neg(void) {
    assert_eq_i64(__negdi2(100), -100, "neg 100");
    assert_eq_i64(__negdi2(-100), 100, "neg -100");
    assert_eq_i64(__negdi2(0), 0, "neg 0");
}

void run_div64_tests(void) {
    kprint("\n=== DIV64 TESTS ===\n");
    failed_tests = 0;

    test_unsigned_div();
    test_unsigned_mod();
    test_signed_div();
    test_signed_mod();
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
