#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <../src/stdlib.c>
#include <sys/mman.h>

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
