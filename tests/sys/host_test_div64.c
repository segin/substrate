#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <limits.h>
#include <time.h>

// Mock __builtin_trap to exit with error instead of crashing, for better test reporting?
// But __builtin_trap is compiler builtin.
// If we want to catch it, we can redefine it?
// No, let's just avoid triggering it.

// Include the implementation directly
#include "../../sys/lib/div64.c"

void test_udiv64() {
    printf("Testing udiv64...\n");
    uint64_t rem;
    uint64_t q;

    // Basic division
    q = udiv64(100, 10, &rem);
    assert(q == 10);
    assert(rem == 0);

    q = udiv64(100, 3, &rem);
    assert(q == 33);
    assert(rem == 1);

    // Division by 1
    q = udiv64(12345, 1, &rem);
    assert(q == 12345);
    assert(rem == 0);

    // Division of 0
    q = udiv64(0, 999, &rem);
    assert(q == 0);
    assert(rem == 0);

    // Large numbers
    uint64_t large = 0xFFFFFFFFFFFFFFFFULL;
    q = udiv64(large, 2, &rem);
    assert(q == 0x7FFFFFFFFFFFFFFFULL);
    assert(rem == 1);

    q = udiv64(large, large, &rem);
    assert(q == 1);
    assert(rem == 0);

    // Edge case: divisor > dividend
    q = udiv64(10, 20, &rem);
    assert(q == 0);
    assert(rem == 10);

    // Null remainder
    q = udiv64(100, 10, NULL);
    assert(q == 10);

    printf("PASS\n");
}

void test_udivdi3_explicit() {
    printf("Testing __udivdi3 (explicit wrapper)...\n");
    assert(__udivdi3(100, 10) == 10);
    assert(__udivdi3(100, 3) == 33);
    assert(__udivdi3(0xFFFFFFFFFFFFFFFFULL, 2) == 0x7FFFFFFFFFFFFFFFULL);
    printf("PASS\n");
}

void test_divdi3() {
    printf("Testing __divdi3 (signed division)...\n");

    // Positive / Positive
    assert(__divdi3(100, 10) == 10);
    assert(__divdi3(100, 3) == 33);

    // Positive / Negative
    assert(__divdi3(100, -10) == -10);
    assert(__divdi3(100, -3) == -33);

    // Negative / Positive
    assert(__divdi3(-100, 10) == -10);
    assert(__divdi3(-100, 3) == -33);

    // Negative / Negative
    assert(__divdi3(-100, -10) == 10);
    assert(__divdi3(-100, -3) == 33);

    // Large negative
    int64_t min = INT64_MIN; // -2^63
    // min / 1 = min
    assert(__divdi3(min, 1) == min);

    // min / 2 = -2^62
    assert(__divdi3(min, 2) == -(1LL << 62));

    // min / -2 = 2^62
    assert(__divdi3(min, -2) == (1LL << 62));

    // Note: min / -1 traps (overflow), so we don't test it.

    printf("PASS\n");
}

void test_moddi3() {
    printf("Testing __moddi3 (signed modulo)...\n");

    // 10 % 3 = 1
    assert(__moddi3(10, 3) == 1);

    // 10 % -3 = 1  (C99 defines truncation towards zero)
    assert(__moddi3(10, -3) == 1);

    // -10 % 3 = -1
    assert(__moddi3(-10, 3) == -1);

    // -10 % -3 = -1
    assert(__moddi3(-10, -3) == -1);

    printf("PASS\n");
}

void test_umoddi3() {
    printf("Testing __umoddi3 (unsigned modulo)...\n");
    assert(__umoddi3(10, 3) == 1);
    assert(__umoddi3(100, 10) == 0);
    assert(__umoddi3(0xFFFFFFFFFFFFFFFFULL, 2) == 1);
    printf("PASS\n");
}

void test_ashldi3() {
    printf("Testing __ashldi3 (left shift)...\n");
    uint64_t val = 1;
    assert(__ashldi3(val, 0) == 1);
    assert(__ashldi3(val, 1) == 2);
    assert(__ashldi3(val, 32) == (1ULL << 32));
    assert(__ashldi3(val, 63) == (1ULL << 63));
    assert(__ashldi3(val, 64) == 1); // Masking behavior

    val = 0xFFFFFFFFFFFFFFFFULL;
    assert(__ashldi3(val, 1) == 0xFFFFFFFFFFFFFFFEULL);
    printf("PASS\n");
}

void test_lshrdi3() {
    printf("Testing __lshrdi3 (logical right shift)...\n");
    uint64_t val = 0x8000000000000000ULL;
    assert(__lshrdi3(val, 0) == val);
    assert(__lshrdi3(val, 1) == 0x4000000000000000ULL);
    assert(__lshrdi3(val, 63) == 1);
    assert(__lshrdi3(val, 64) == val); // Masking behavior

    val = 0xFFFFFFFFFFFFFFFFULL;
    assert(__lshrdi3(val, 1) == 0x7FFFFFFFFFFFFFFFULL);
    printf("PASS\n");
}

void test_ashrdi3() {
    printf("Testing __ashrdi3 (arithmetic right shift)...\n");
    int64_t val = -2LL; // 0xFF...FE
    assert(__ashrdi3(val, 1) == -1LL); // 0xFF...FF

    val = 0x8000000000000000LL; // INT64_MIN
    assert(__ashrdi3(val, 1) == (int64_t)0xC000000000000000ULL); // Sign extended

    val = 100;
    assert(__ashrdi3(val, 1) == 50);
    assert(__ashrdi3(val, 64) == val); // Masking behavior
    printf("PASS\n");
}

void test_muldi3() {
    printf("Testing __muldi3 (multiplication)...\n");

    // Basic cases
    assert(__muldi3(10, 10) == 100);
    assert(__muldi3(10, -10) == -100);
    assert(__muldi3(-10, -10) == 100);
    assert(__muldi3(0, 12345) == 0);
    assert(__muldi3(12345, 0) == 0);

    // Edge cases
    assert(__muldi3(1, 123456789) == 123456789);
    assert(__muldi3(-1, 123456789) == -123456789);
    assert(__muldi3(INT64_MAX, 1) == INT64_MAX);
    assert(__muldi3(INT64_MIN, 1) == INT64_MIN);

    // Overflow/Wrap-around cases (valid in 2's complement)
    // INT64_MAX * -1 = -INT64_MAX = -(2^63 - 1) = -2^63 + 1 = INT64_MIN + 1
    assert(__muldi3(INT64_MAX, -1) == -INT64_MAX);

    // INT64_MIN * -1 -> Overflow 2^63 -> Wraps to -2^63 (INT64_MIN)
    // (unsigned)INT64_MIN = 0x8000...0000
    // (unsigned)-1 = 0xFFFF...FFFF
    // Product = 0x8000...0000 << 64 - 0x8000...0000 = 0x8000...0000 (low 64 bits)
    assert(__muldi3(INT64_MIN, -1) == INT64_MIN);

    // Large numbers and bit patterns
    // 2^32 * 2 = 2^33
    assert(__muldi3(0x100000000LL, 2) == 0x200000000LL);

    // (2^32 - 1) * (2^32 - 1) = 2^64 - 2*2^32 + 1 -> wraps to -2*2^32 + 1
    // 0xFFFFFFFF * 0xFFFFFFFF = 0xFFFFFFFE00000001
    assert(__muldi3(0xFFFFFFFFLL, 0xFFFFFFFFLL) == 0xFFFFFFFE00000001LL);

    // Cross product test (exercising the hi_lo + lo_hi logic)
    // A = 2^32 + 1, B = 2^32 + 2
    // A * B = (2^32 * 2^32) + 2*2^32 + 1*2^32 + 2 = 2^64 + 3*2^32 + 2 -> wraps to 3*2^32 + 2
    int64_t bigA = (1LL << 32) + 1;
    int64_t bigB = (1LL << 32) + 2;
    int64_t expected = (3LL << 32) + 2;
    assert(__muldi3(bigA, bigB) == expected);

    // Randomized testing against host multiplication
    printf("  > Running 100,000 randomized multiplication tests...\n");
    srand(time(NULL));
    for (int i = 0; i < 100000; i++) {
        uint64_t ua = ((uint64_t)rand() << 32) | rand();
        uint64_t ub = ((uint64_t)rand() << 32) | rand();
        int64_t a = (int64_t)ua;
        int64_t b = (int64_t)ub;

        int64_t host_res = a * b;
        int64_t lib_res = __muldi3(a, b);

        if (host_res != lib_res) {
            printf("FAIL: %lld * %lld = %lld (expected %lld)\n",
                   (long long)a, (long long)b, (long long)lib_res, (long long)host_res);
            assert(0);
        }
    }

    printf("PASS\n");
}

void test_negdi2() {
    printf("Testing __negdi2 (negate)...\n");
    assert(__negdi2(10) == -10);
    assert(__negdi2(-10) == 10);
    assert(__negdi2(0) == 0);
    assert(__negdi2(INT64_MIN) == INT64_MIN); // -(-2^63) = -2^63 due to overflow
    printf("PASS\n");
}

int main() {
    test_udiv64();
    test_udivdi3_explicit();
    test_divdi3();
    test_moddi3();
    test_umoddi3();
    test_ashldi3();
    test_lshrdi3();
    test_ashrdi3();
    test_muldi3();
    test_negdi2();
    printf("All host tests passed!\n");
    return 0;
}
