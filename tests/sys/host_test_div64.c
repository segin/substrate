#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <limits.h>

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

void test_shifts() {
    printf("Testing shifts...\n");

    // Logical right shift (__lshrdi3)
    assert(__lshrdi3(0xFFFFFFFFFFFFFFFFULL, 0) == 0xFFFFFFFFFFFFFFFFULL);
    assert(__lshrdi3(0xFFFFFFFFFFFFFFFFULL, 1) == 0x7FFFFFFFFFFFFFFFULL);
    assert(__lshrdi3(0xFFFFFFFFFFFFFFFFULL, 32) == 0x00000000FFFFFFFFULL);
    assert(__lshrdi3(0xFFFFFFFFFFFFFFFFULL, 63) == 1ULL);
    assert(__lshrdi3(0xFFFFFFFFFFFFFFFFULL, 64) == 0xFFFFFFFFFFFFFFFFULL); // 64 & 63 == 0

    // Left shift (__ashldi3)
    assert(__ashldi3(1ULL, 0) == 1ULL);
    assert(__ashldi3(1ULL, 1) == 2ULL);
    assert(__ashldi3(1ULL, 32) == 0x100000000ULL);
    assert(__ashldi3(1ULL, 63) == 0x8000000000000000ULL);
    assert(__ashldi3(1ULL, 64) == 1ULL);

    // Arithmetic right shift (__ashrdi3)
    assert(__ashrdi3(0x7FFFFFFFFFFFFFFFULL, 1) == 0x3FFFFFFFFFFFFFFFULL);
    assert(__ashrdi3(0x8000000000000000LL, 1) == 0xC000000000000000LL);
    assert(__ashrdi3(0xFFFFFFFFFFFFFFFFLL, 1) == 0xFFFFFFFFFFFFFFFFLL);
    assert(__ashrdi3(0x8000000000000000LL, 63) == 0xFFFFFFFFFFFFFFFFLL);

    printf("PASS\n");
}

void test_mul() {
    printf("Testing __muldi3 (multiplication)...\n");
    assert(__muldi3(10, 10) == 100);
    assert(__muldi3(-10, 10) == -100);
    assert(__muldi3(10, -10) == -100);
    assert(__muldi3(-10, -10) == 100);
    assert(__muldi3(0x100000000LL, 2) == 0x200000000LL);
    assert(__muldi3(0xFFFFFFFFLL, 0xFFFFFFFFLL) == 0xFFFFFFFE00000001LL);
    printf("PASS\n");
}

void test_neg() {
    printf("Testing __negdi2 (negation)...\n");
    assert(__negdi2(100) == -100);
    assert(__negdi2(-100) == 100);
    assert(__negdi2(0) == 0);
    assert(__negdi2(INT64_MIN) == INT64_MIN); // -(-2^63) = -2^63 due to overflow
    printf("PASS\n");
}

int main() {
    test_udiv64();
    test_divdi3();
    test_moddi3();
    test_umoddi3();
    test_shifts();
    test_mul();
    test_neg();
    printf("All host tests passed!\n");
    return 0;
}
