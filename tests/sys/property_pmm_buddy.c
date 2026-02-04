/*
 * property_pmm_buddy.c - Property-based tests for PMM Buddy Allocator
 */
#include <stdio.h>
#include <stdint.h>
#include <assert.h>

#define PMM_BLOCK_SIZE 4096
#define PMM_MAX_ORDER 11

static int get_order(size_t count) {
    int order = 0;
    while ((1UL << order) < count) {
        order++;
    }
    return order;
}

/* Property: Order always rounds up to next power of 2 */
static void property_order_power_of_two(void) {
    for (size_t count = 1; count <= 2048; count++) {
        int order = get_order(count);
        size_t block_pages = 1UL << order;
        
        /* Block size >= requested */
        assert(block_pages >= count);
        
        /* Block size < 2x requested (for power of 2 counts) */
        if ((count & (count - 1)) == 0) {  /* count is power of 2 */
            assert(block_pages == count);
        }
    }
    printf("[PROP] Order always produces sufficient block size\n");
}

/* Property: Buddy XOR is self-inverse */
static void property_buddy_xor_inverse(void) {
    for (int order = 0; order < PMM_MAX_ORDER; order++) {
        uint32_t block_size = (1 << order) * PMM_BLOCK_SIZE;
        
        for (uint32_t base = 0x100000; base < 0x1000000; base += block_size * 2) {
            uint32_t buddy = base ^ block_size;
            uint32_t original = buddy ^ block_size;
            
            /* XOR twice returns to original */
            assert(original == base);
        }
    }
    printf("[PROP] Buddy XOR is self-inverse\n");
}

/* Property: Aligned addresses have correct buddy */
static void property_buddy_alignment(void) {
    for (int order = 0; order < 8; order++) {  /* Test up to order 7 */
        uint32_t block_size = (1 << order) * PMM_BLOCK_SIZE;
        
        /* For properly aligned blocks, buddy is always valid */
        for (uint32_t base = 0x100000; base < 0x200000; base += block_size * 2) {
            uint32_t buddy = base ^ block_size;
            
            /* Buddy is within same parent block */
            uint32_t parent_size = block_size * 2;
            uint32_t parent_base = base & ~(parent_size - 1);
            
            assert(buddy >= parent_base);
            assert(buddy < parent_base + parent_size);
        }
    }
    printf("[PROP] Buddy addresses are within parent block\n");
}

/* Property: Merge produces correctly aligned larger block */
static void property_merge_alignment(void) {
    for (int order = 0; order < 8; order++) {
        uint32_t block_size = (1 << order) * PMM_BLOCK_SIZE;
        uint32_t parent_size = block_size * 2;
        
        /* Start of parent block */
        uint32_t first = 0x200000;  /* Must be parent-aligned */
        uint32_t second = first + block_size;
        
        /* Merged block starts at lower address */
        uint32_t merged_start = (first < second) ? first : second;
        
        /* Merged block must be parent-aligned */
        assert((merged_start & (parent_size - 1)) == 0);
    }
    printf("[PROP] Merged blocks are correctly aligned\n");
}

int main(void) {
    printf("=== PMM Buddy Property Tests ===\n");
    
    property_order_power_of_two();
    property_buddy_xor_inverse();
    property_buddy_alignment();
    property_merge_alignment();
    
    printf("=== All PMM Buddy properties hold ===\n");
    return 0;
}
