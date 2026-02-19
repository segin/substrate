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
#include <time.h>

/* Mock environment for freestanding div64.c */
/* We don't expect __builtin_trap to be called for negation, but define it just in case */
#define __builtin_trap() { fprintf(stderr, "TRAP\n"); exit(1); }

/* Include the implementation directly to test it in isolation */
#include "../../sys/lib/div64.c"

/* Helper to generate full 64-bit random numbers */
static uint64_t rand64(void) {
    uint64_t r = 0;
    /* Combine 15-bit chunks from rand() to cover full 64-bit range */
    r |= ((uint64_t)rand() & 0x7FFF);
    r |= ((uint64_t)rand() & 0x7FFF) << 15;
    r |= ((uint64_t)rand() & 0x7FFF) << 30;
    r |= ((uint64_t)rand() & 0x7FFF) << 45;
    r |= ((uint64_t)rand() & 0xF) << 60;
    return r;
}

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

static void test_negdi2_randomized(void) {
    printf("Testing randomized property-based verification (1,000,000 iterations)...\n");

    for (int i = 0; i < 1000000; i++) {
        int64_t val = (int64_t)rand64();
        int64_t expected = -val;
        int64_t actual = __negdi2(val);

        if (actual != expected) {
            fprintf(stderr, "FAIL: iteration %d, input %lld (0x%llx), expected %lld, got %lld\n",
                    i, (long long)val, (unsigned long long)val,
                    (long long)expected, (long long)actual);
            exit(1);
        }
    }
}

int main(void) {
    printf("=== TEST: __negdi2 ===\n");

    unsigned int seed = (unsigned int)time(NULL);
    printf("Seed: %u\n", seed);
    srand(seed);

    test_negdi2_basic();
    test_negdi2_large();
    test_negdi2_edge_cases();
    test_negdi2_randomized();

    printf("PASS\n");
    return 0;
}
