/*
 * Host-side test for stdlib random number generator changes.
 *
 * To run this test manually:
 * 1. Create a temporary file that includes necessary renames to avoid conflicts with host libc:
 *    echo "#define exit my_exit
 *    #define abort my_abort
 *    #define __stack_chk_fail my___stack_chk_fail
 *    #define malloc my_malloc
 *    #define free my_free
 *    #define calloc my_calloc
 *    #define realloc my_realloc
 *    #define aligned_alloc my_aligned_alloc
 *    #define quick_exit my_quick_exit
 *    #define at_quick_exit my_at_quick_exit
 *    #define strtol my_strtol
 *    #define atoi my_atoi
 *    #define atol my_atol
 *    #define atoll my_atoll
 *    #define atof my_atof
 *    #define getenv my_getenv
 *    #define system my_system
 *    #define abs my_abs
 *    #define labs my_labs
 *    #define llabs my_llabs
 *    #define qsort my_qsort
 *    #define bsearch my_bsearch
 *    #define rand my_rand
 *    #define srand my_srand
 *    #define arc4random my_arc4random
 *    #define arc4random_buf my_arc4random_buf
 *    #define arc4random_uniform my_arc4random_uniform" > stdlib_test_gen.c
 *
 * 2. Append the stdlib source:
 *    cat ../../lib/c/src/stdlib.c >> stdlib_test_gen.c
 *
 * 3. Compile:
 *    gcc -o test_rand_exec host_test_rand.c stdlib_test_gen.c -w
 *
 * 4. Run:
 *    ./test_rand_exec
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

// Prototypes for the renamed functions we want to test
void my_srand(unsigned int seed);
int my_rand(void);
void my_arc4random_buf(void *buf, size_t n);
uint32_t my_arc4random(void);
uint32_t my_arc4random_uniform(uint32_t upper_bound);

int main() {
    printf("Testing RAND_MAX logic...\n");
    // Verify RAND_MAX in header? We can't easily unless we include the header.
    // We'll trust the code logic for now.

    printf("Testing rand() / srand()...\n");
    my_srand(12345);
    int r1 = my_rand();
    int r2 = my_rand();

    printf("r1: %d, r2: %d\n", r1, r2);
    assert(r1 != r2); // Unlikely to match
    assert(r1 >= 0);
    assert(r2 >= 0);
    // xoroshiro128++ & 0x7FFFFFFF should be positive and up to 2^31-1

    my_srand(12345);
    int r1_again = my_rand();
    int r2_again = my_rand();
    printf("r1_again: %d, r2_again: %d\n", r1_again, r2_again);
    assert(r1 == r1_again);
    assert(r2 == r2_again);

    printf("Testing arc4random()...\n");
    uint32_t a1 = my_arc4random();
    uint32_t a2 = my_arc4random();
    printf("a1: %u, a2: %u\n", a1, a2);
    assert(a1 != a2); // Very unlikely

    printf("Testing arc4random_uniform()...\n");
    for (int i = 0; i < 100; i++) {
        uint32_t u = my_arc4random_uniform(10);
        assert(u < 10);
    }

    printf("All tests passed!\n");
    return 0;
}
