/*
 * Host-side test for __negdi2() in sys/lib/div64.c
 *
 * This test validates the correctness of the 64-bit negation helper.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <limits.h>

/* Mock environment for freestanding div64.c */
/* We don't expect __builtin_trap to be called for negation, but define it just in case */
#define __builtin_trap() { fprintf(stderr, "TRAP\n"); exit(1); }

/* Include the implementation directly to test it in isolation */
#include "../../sys/lib/div64.c"

static void test_negdi2_basic(void) {
    printf("Testing basic negation...\n");
    assert(__negdi2(0) == 0);
    assert(__negdi2(1) == -1);
    assert(__negdi2(-1) == 1);
    assert(__negdi2(42) == -42);
    assert(__negdi2(-100) == 100);
}

static void test_negdi2_large(void) {
    printf("Testing large number negation...\n");
    assert(__negdi2(INT64_MAX) == -INT64_MAX);
    assert(__negdi2(-INT64_MAX) == INT64_MAX);

    int64_t large = 0x1234567890ABCDEF;
    assert(__negdi2(large) == -large);
}

static void test_negdi2_edge_cases(void) {
    printf("Testing edge cases...\n");
    /*
     * Negating INT64_MIN (-2^63) is a special case in 2's complement.
     * It theoretically overflows, but in C (and on x86), it typically wraps back to INT64_MIN.
     * -(-9223372036854775808) = 9223372036854775808, which is 0x8000...0000.
     * As a signed 64-bit int, 0x8000...0000 is -9223372036854775808.
     */
    int64_t min = INT64_MIN;
    assert(__negdi2(min) == min);
}

int main(void) {
    printf("=== TEST: __negdi2 ===\n");

    test_negdi2_basic();
    test_negdi2_large();
    test_negdi2_edge_cases();

    printf("PASS\n");
    return 0;
}
