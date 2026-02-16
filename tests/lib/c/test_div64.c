#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <limits.h>

// Rename functions to avoid conflicts with host libc
#define __udivdi3 tested_udivdi3
#define __umoddi3 tested_umoddi3
#define __divdi3 tested_divdi3
#define __moddi3 tested_moddi3

// Include the source file directly
#include "../../../lib/c/src/div64.c"

// Forward declarations of the renamed functions
uint64_t tested_udivdi3(uint64_t n, uint64_t d);
uint64_t tested_umoddi3(uint64_t n, uint64_t d);
int64_t tested_divdi3(int64_t a, int64_t b);
int64_t tested_moddi3(int64_t a, int64_t b);

void test_udiv64_static(void) {
    printf("Testing static udiv64...\n");
    uint64_t rem;
    uint64_t q;

    // 10 / 3 = 3 rem 1
    q = udiv64(10, 3, &rem);
    assert(q == 3);
    assert(rem == 1);

    // 20 / 5 = 4 rem 0
    q = udiv64(20, 5, &rem);
    assert(q == 4);
    assert(rem == 0);

    // Max uint64 / 2
    uint64_t max = UINT64_MAX;
    q = udiv64(max, 2, &rem);
    assert(q == max / 2);
    assert(rem == max % 2);

    // Division by 1
    q = udiv64(12345, 1, &rem);
    assert(q == 12345);
    assert(rem == 0);

    // Null remainder pointer
    q = udiv64(100, 10, NULL);
    assert(q == 10);

    printf("udiv64 passed\n");
}

void test_udivdi3(void) {
    printf("Testing __udivdi3...\n");
    assert(tested_udivdi3(10, 2) == 5);
    assert(tested_udivdi3(10, 3) == 3);
    assert(tested_udivdi3(0, 5) == 0);
    assert(tested_udivdi3(UINT64_MAX, 1) == UINT64_MAX);
    assert(tested_udivdi3(UINT64_MAX, UINT64_MAX) == 1);

    // Large numbers
    uint64_t big = 1000000000000ULL;
    assert(tested_udivdi3(big, 1000) == 1000000000ULL);

    printf("__udivdi3 passed\n");
}

void test_umoddi3(void) {
    printf("Testing __umoddi3...\n");
    assert(tested_umoddi3(10, 3) == 1);
    assert(tested_umoddi3(10, 2) == 0);
    assert(tested_umoddi3(0, 5) == 0);
    assert(tested_umoddi3(UINT64_MAX, UINT64_MAX) == 0);
    assert(tested_umoddi3(UINT64_MAX, 2) == 1); // Odd number

    printf("__umoddi3 passed\n");
}

void test_divdi3(void) {
    printf("Testing __divdi3...\n");
    // Positive / Positive
    assert(tested_divdi3(10, 3) == 3);

    // Positive / Negative
    assert(tested_divdi3(10, -3) == -3);

    // Negative / Positive
    assert(tested_divdi3(-10, 3) == -3);

    // Negative / Negative
    assert(tested_divdi3(-10, -3) == 3);

    // Zero
    assert(tested_divdi3(0, -5) == 0);

    // Edge cases
    assert(tested_divdi3(INT64_MAX, 1) == INT64_MAX);
    assert(tested_divdi3(INT64_MAX, -1) == -INT64_MAX);
    assert(tested_divdi3(INT64_MIN, 1) == INT64_MIN);

    // Division by -1 for INT64_MIN is undefined/traps in implementation, skip it.

    printf("__divdi3 passed\n");
}

void test_moddi3(void) {
    printf("Testing __moddi3...\n");
    // Positive % Positive
    assert(tested_moddi3(10, 3) == 1);

    // Positive % Negative
    assert(tested_moddi3(10, -3) == 1);

    // Negative % Positive
    assert(tested_moddi3(-10, 3) == -1);

    // Negative % Negative
    assert(tested_moddi3(-10, -3) == -1);

    // Zero
    assert(tested_moddi3(0, -5) == 0);

    printf("__moddi3 passed\n");
}

int main(void) {
    printf("Running div64 tests...\n");
    test_udiv64_static();
    test_udivdi3();
    test_umoddi3();
    test_divdi3();
    test_moddi3();
    printf("All div64 tests passed!\n");
    return 0;
}
