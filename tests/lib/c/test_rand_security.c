#include <stdio.h>
#include <assert.h>
#include <string.h>

// Rename standard library functions to avoid conflicts with host libc
#define exit tested_exit
#define abort tested_abort
#define __stack_chk_fail tested_stack_chk_fail
#define malloc tested_malloc
#define free tested_free
#define calloc tested_calloc
#define realloc tested_realloc
#define aligned_alloc tested_aligned_alloc
#define quick_exit tested_quick_exit
#define at_quick_exit tested_at_quick_exit
#define strtol tested_strtol
#define atoi tested_atoi
#define atol tested_atol
#define atoll tested_atoll
#define atof tested_atof
#define getenv tested_getenv
#define system tested_system
#define abs tested_abs
#define labs tested_labs
#define llabs tested_llabs
#define qsort tested_qsort
#define bsearch tested_bsearch
#define rand tested_rand
#define srand tested_srand
#define arc4random_buf tested_arc4random_buf
#define arc4random tested_arc4random
#define arc4random_uniform tested_arc4random_uniform

// Include the source file directly.
// Assuming this is compiled from the repository root with -Itests/lib/c
#include "lib/c/src/stdlib.c"

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--verify-determinism") == 0) {
        // Test determinism within a single run
        tested_srand(12345);
        int r1 = tested_rand();
        int r2 = tested_rand();

        tested_srand(12345);
        int r1_again = tested_rand();
        int r2_again = tested_rand();

        if (r1 == r1_again && r2 == r2_again) {
            printf("DETERMINISTIC_WITHIN_RUN\n");
            return 0;
        } else {
            printf("FAILED_DETERMINISM_WITHIN_RUN\n");
            return 1;
        }
    }

    // Default behavior: print a few random numbers for a fixed seed
    tested_srand(12345);
    for (int i = 0; i < 5; i++) {
        printf("%d ", tested_rand());
    }
    printf("\n");
    return 0;
}
