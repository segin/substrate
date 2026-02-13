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

int main() {
    test_udiv64();
    test_divdi3();
    test_moddi3();
    test_umoddi3();
    printf("All host tests passed!\n");
    return 0;
}
