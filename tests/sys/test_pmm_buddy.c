/*
 * test_pmm_buddy.c - Unit tests for PMM buddy arithmetic helpers
 */
#include <stdio.h>
#include <stdint.h>
#include <kern/console.h>

#define PMM_BLOCK_SIZE 4096U
#define PMM_MAX_ORDER 11

static int passed = 0;
static int failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        kprint("[FAIL] "); kprint(msg); kprint("\n"); \
        failed++; \
        return; \
    } \
} while (0)

#define TEST_PASS(msg) do { \
    kprint("[PASS] "); kprint(msg); kprint("\n"); \
    passed++; \
} while (0)

static int get_order(size_t count) {
    int order = 0;
    while (((size_t)1U << order) < count) {
        order++;
    }
    return order;
}

static void test_order_calculation(void) {
    TEST_ASSERT(get_order(1) == 0, "order: 1 page -> order 0");
    TEST_ASSERT(get_order(2) == 1, "order: 2 pages -> order 1");
    TEST_ASSERT(get_order(3) == 2, "order: 3 pages rounds up to order 2");
    TEST_ASSERT(get_order(4) == 2, "order: 4 pages -> order 2");
    TEST_ASSERT(get_order(5) == 3, "order: 5 pages rounds up to order 3");
    TEST_ASSERT(get_order(1024) == 10, "order: 1024 pages -> order 10");
    TEST_PASS("order_calculation");
}

static void test_buddy_address_xor(void) {
    uint32_t pa = 0x100000U;
    uint32_t buddy = pa ^ (1U << 0) * PMM_BLOCK_SIZE;
    TEST_ASSERT(buddy == 0x101000U, "buddy: order0 XOR");

    buddy = pa ^ (1U << 1) * PMM_BLOCK_SIZE;
    TEST_ASSERT(buddy == 0x102000U, "buddy: order1 XOR");

    buddy = pa ^ (1U << 2) * PMM_BLOCK_SIZE;
    TEST_ASSERT(buddy == 0x104000U, "buddy: order2 XOR");

    TEST_PASS("buddy_address_xor");
}

static void test_block_alignment(void) {
    for (int order = 0; order < PMM_MAX_ORDER; order++) {
        uint32_t block_size = (1U << order) * PMM_BLOCK_SIZE;
        uint32_t aligned_addr = 0x100000U;
        TEST_ASSERT((aligned_addr & (block_size - 1U)) == 0, "alignment: block alignment failed");
    }
    TEST_PASS("block_alignment");
}

static void test_split_logic(void) {
    uint32_t base = 0x200000U; /* 2MB aligned */
    int order = 3;             /* 32KB block */
    uint32_t first_half = base;
    uint32_t second_half = base + ((1U << (order - 1)) * PMM_BLOCK_SIZE);

    TEST_ASSERT(first_half == 0x200000U, "split: first half wrong");
    TEST_ASSERT(second_half == 0x204000U, "split: second half wrong");
    TEST_PASS("split_logic");
}

void test_pmm_buddy(void) {
    kprint("=== PMM Buddy Unit Tests ===\n");

    test_order_calculation();
    test_buddy_address_xor();
    test_block_alignment();
    test_split_logic();

    char buf[64];
    sprintf(buf, "=== pmm_buddy tests: %d passed, %d failed ===\n", passed, failed);
    kprint(buf);
}
