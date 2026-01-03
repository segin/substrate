/*
 * test_pmm_buddy.c - Unit tests for PMM Buddy Allocator
 */
#include <stdio.h>
#include <stdint.h>
#include <assert.h>

#define PMM_BLOCK_SIZE 4096
#define PMM_MAX_ORDER 11

/* Test: Order calculation */
static int get_order(size_t count) {
    int order = 0;
    while ((1UL << order) < count) {
        order++;
    }
    return order;
}

static void test_order_calculation(void) {
    assert(get_order(1) == 0);    /* 1 page = order 0 */
    assert(get_order(2) == 1);    /* 2 pages = order 1 */
    assert(get_order(3) == 2);    /* 3 pages = order 2 (rounds up) */
    assert(get_order(4) == 2);    /* 4 pages = order 2 */
    assert(get_order(5) == 3);    /* 5 pages = order 3 (rounds up) */
    assert(get_order(1024) == 10); /* 1024 pages = order 10 */
    printf("[PASS] Order calculation\n");
}

/* Test: Buddy address calculation (XOR formula) */
static void test_buddy_address(void) {
    /* For order 0 (4KB blocks), buddy is at pa ^ 4096 */
    uint32_t pa = 0x100000;  /* 1MB */
    uint32_t buddy = pa ^ (1 << 0) * PMM_BLOCK_SIZE;
    assert(buddy == 0x101000);
    
    /* For order 1 (8KB blocks), buddy is at pa ^ 8192 */
    pa = 0x100000;
    buddy = pa ^ (1 << 1) * PMM_BLOCK_SIZE;
    assert(buddy == 0x102000);
    
    /* For order 2 (16KB blocks), buddy is at pa ^ 16384 */
    pa = 0x100000;
    buddy = pa ^ (1 << 2) * PMM_BLOCK_SIZE;
    assert(buddy == 0x104000);
    
    printf("[PASS] Buddy address calculation\n");
}

/* Test: Block alignment for orders */
static void test_block_alignment(void) {
    /* Order N blocks must be aligned to (1 << N) * PAGE_SIZE */
    for (int order = 0; order < PMM_MAX_ORDER; order++) {
        uint32_t block_size = (1 << order) * PMM_BLOCK_SIZE;
        uint32_t aligned_addr = 0x100000;  /* 1MB - good alignment */
        
        /* Check alignment */
        assert((aligned_addr & (block_size - 1)) == 0);
    }
    printf("[PASS] Block alignment\n");
}

/* Test: Split correctness */
static void test_split_logic(void) {
    /* When splitting order N, we get two order N-1 blocks */
    /* First half at original address, second half at addr + (1 << (N-1)) * PAGE_SIZE */
    
    uint32_t base = 0x200000;  /* 2MB aligned */
    int order = 3;  /* 32KB block */
    
    /* After split, we should have two 16KB blocks */
    uint32_t first_half = base;
    uint32_t second_half = base + ((1 << (order - 1)) * PMM_BLOCK_SIZE);
    
    assert(first_half == 0x200000);
    assert(second_half == 0x204000);  /* 16KB offset */
    
    printf("[PASS] Split logic\n");
}

int main(void) {
    printf("=== PMM Buddy Allocator Unit Tests ===\n");
    
    test_order_calculation();
    test_buddy_address();
    test_block_alignment();
    test_split_logic();
    
    printf("=== All PMM Buddy tests passed ===\n");
    return 0;
}
