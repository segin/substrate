#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <assert.h>
#include <unistd.h>

// Rename functions to avoid conflict with host libc
#define malloc my_malloc
#define free my_free
#define calloc my_calloc
#define realloc my_realloc
#define aligned_alloc my_aligned_alloc
#define exit my_exit
#define abort my_abort
#define __stack_chk_fail my_stack_chk_fail
#define quick_exit my_quick_exit
#define at_quick_exit my_at_quick_exit
#define strtol my_strtol
#define atoi my_atoi
#define atol my_atol
#define atoll my_atoll
#define atof my_atof
#define getenv my_getenv
#define system my_system
#define abs my_abs
#define labs my_labs
#define llabs my_llabs
#define qsort my_qsort
#define bsearch my_bsearch
#define srand my_srand
#define rand my_rand
#define arc4random_buf my_arc4random_buf
#define arc4random my_arc4random
#define arc4random_uniform my_arc4random_uniform

// Include the source file directly
// We use -I include to pick repo headers which match implementation
#include "../src/stdlib.c"

#undef malloc
#undef free
#undef calloc
#undef realloc
#undef aligned_alloc
#undef exit
#undef abort
#undef __stack_chk_fail
#undef quick_exit
#undef at_quick_exit
#undef strtol
#undef atoi
#undef atol
#undef atoll
#undef atof
#undef getenv
#undef system
#undef abs
#undef labs
#undef llabs
#undef qsort
#undef bsearch
#undef srand
#undef rand
#undef arc4random_buf
#undef arc4random
#undef arc4random_uniform

#define NUM_ALLOCS 10000
#define MAX_SIZE 1024

int main(void) {
    void *ptrs[NUM_ALLOCS];
    clock_t start, end;
    double cpu_time_used;

    printf("Starting malloc benchmark...\n");

    // Phase 1: Sequential Allocation
    start = clock();
    for (int i = 0; i < NUM_ALLOCS; i++) {
        size_t size = (i % MAX_SIZE) + 1;
        ptrs[i] = my_malloc(size);
        // Original allocator has 1MB limit. 10000 * 512 = ~5MB.
        // It will return NULL after ~2000 allocs.
        if (ptrs[i]) {
            memset(ptrs[i], 0xAA, size);
        }
    }
    end = clock();
    cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
    printf("Phase 1 (Alloc): %f seconds\n", cpu_time_used);

    // Phase 2: Sequential Free
    start = clock();
    for (int i = 0; i < NUM_ALLOCS; i++) {
        if (ptrs[i]) {
            my_free(ptrs[i]);
        }
    }
    end = clock();
    cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
    printf("Phase 2 (Free): %f seconds\n", cpu_time_used);

    // Phase 3: Re-allocation
    int success_count = 0;
    start = clock();
    for (int i = 0; i < NUM_ALLOCS; i++) {
        size_t size = (i % MAX_SIZE) + 1;
        ptrs[i] = my_malloc(size);
        if (ptrs[i]) {
            memset(ptrs[i], 0xBB, size);
            success_count++;
        }
    }
    end = clock();
    cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
    printf("Phase 3 (Re-alloc): %f seconds, Success count: %d/%d\n", cpu_time_used, success_count, NUM_ALLOCS);

    // Cleanup
    for (int i = 0; i < NUM_ALLOCS; i++) {
        if (ptrs[i]) my_free(ptrs[i]);
    }

    return 0;
}
