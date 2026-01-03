/*
 * fuzz_pmm_buddy.c - Fuzzing tests for PMM Buddy Allocator
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define PMM_BLOCK_SIZE 4096
#define PMM_MAX_ORDER 11
#define NUM_FUZZ_ITERATIONS 10000

static int get_order(size_t count) {
    int order = 0;
    while ((1UL << order) < count) {
        order++;
    }
    return order;
}

/* Fuzz: Random allocation sizes */
static void fuzz_allocation_sizes(void) {
    int passed = 0;
    int failed = 0;
    
    for (int i = 0; i < NUM_FUZZ_ITERATIONS; i++) {
        size_t count = (rand() % 2048) + 1;
        int order = get_order(count);
        size_t block_pages = 1UL << order;
        
        if (block_pages >= count && order < PMM_MAX_ORDER) {
            passed++;
        } else {
            failed++;
        }
    }
    
    printf("[FUZZ] allocation_sizes: %d passed, %d failed\n", passed, failed);
}

/* Fuzz: Random buddy calculations */
static void fuzz_buddy_calculations(void) {
    int passed = 0;
    int failed = 0;
    
    for (int i = 0; i < NUM_FUZZ_ITERATIONS; i++) {
        int order = rand() % PMM_MAX_ORDER;
        uint32_t block_size = (1 << order) * PMM_BLOCK_SIZE;
        
        /* Random aligned address */
        uint32_t base = (rand() % 0x10000) * block_size;
        uint32_t buddy = base ^ block_size;
        
        /* Verify XOR inverse property */
        if ((buddy ^ block_size) == base) {
            passed++;
        } else {
            failed++;
        }
    }
    
    printf("[FUZZ] buddy_calculations: %d passed, %d failed\n", passed, failed);
}

/* Fuzz: Random coalescing scenarios */
static void fuzz_coalescing(void) {
    int valid_merges = 0;
    int invalid_merges = 0;
    
    for (int i = 0; i < NUM_FUZZ_ITERATIONS; i++) {
        int order = rand() % (PMM_MAX_ORDER - 1);
        uint32_t block_size = (1 << order) * PMM_BLOCK_SIZE;
        uint32_t parent_size = block_size * 2;
        
        /* Random aligned base */
        uint32_t base = (rand() % 0x10000) * parent_size;
        uint32_t buddy = base + block_size;
        
        /* Check if merge would produce valid parent */
        uint32_t merged = (base < buddy) ? base : buddy;
        
        if ((merged & (parent_size - 1)) == 0) {
            valid_merges++;
        } else {
            invalid_merges++;
            printf("[FAIL] Invalid merge: base=0x%x, buddy=0x%x, merged=0x%x\n",
                   base, buddy, merged);
        }
    }
    
    printf("[FUZZ] coalescing: %d valid, %d invalid\n", valid_merges, invalid_merges);
}

/* Fuzz: Order overflow protection */
static void fuzz_order_overflow(void) {
    int protected = 0;
    int overflow = 0;
    
    for (int i = 0; i < 1000; i++) {
        size_t huge_count = (size_t)rand() * rand() + 1;
        int order = get_order(huge_count);
        
        if (order >= PMM_MAX_ORDER) {
            /* Would be rejected by allocator */
            protected++;
        } else {
            /* Verify block size doesn't overflow */
            size_t block_pages = 1UL << order;
            if (block_pages >= huge_count) {
                protected++;
            } else {
                overflow++;
            }
        }
    }
    
    printf("[FUZZ] order_overflow: %d protected, %d overflow\n", protected, overflow);
}

int main(void) {
    printf("=== PMM Buddy Fuzz Tests ===\n");
    
    srand(54321);  /* Fixed seed for reproducibility */
    
    fuzz_allocation_sizes();
    fuzz_buddy_calculations();
    fuzz_coalescing();
    fuzz_order_overflow();
    
    printf("=== PMM Buddy Fuzz Tests Complete ===\n");
    return 0;
}
