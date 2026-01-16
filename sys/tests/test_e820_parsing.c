/*
 * test_e820_parsing.c - Unit tests for e820 memory map parsing
 *
 * Tests pmm_walk_e820 and pmm_init_e820 validation logic including:
 * - All 5 e820 types (USABLE, RESERVED, ACPI, NVS, BAD)
 * - Zero-length entry handling
 * - 64-bit address clamping
 * - Empty map handling
 */
#include <stdio.h>
#include <stdint.h>
#include "../kern/console.h"
#include "../arch/i386/pmm.h"

/* Test context for callback */
struct e820_test_ctx {
    uint32_t region_count;
    uint64_t total_bytes;
    uint32_t last_start;
    uint32_t last_len;
};

static void e820_test_cb(uint32_t start, uint32_t len, void *arg) {
    struct e820_test_ctx *ctx = arg;
    ctx->region_count++;
    ctx->total_bytes += len;
    ctx->last_start = start;
    ctx->last_len = len;
}

static int passed = 0;
static int failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        kprint("[FAIL] "); kprint(msg); kprint("\n"); \
        failed++; \
        return; \
    } \
} while(0)

#define TEST_PASS(msg) do { \
    kprint("[PASS] "); kprint(msg); kprint("\n"); \
    passed++; \
} while(0)

/* Test: Basic valid entry parsing */
static void test_e820_basic_valid_entry(void) {
    e820_entry_t map[1] = {
        { .addr = 0x100000, .len = 0x1000000, .type = E820_USABLE }
    };
    
    struct e820_test_ctx ctx = {0};
    pmm_walk_e820(map, 1, e820_test_cb, &ctx);
    
    TEST_ASSERT(ctx.region_count == 1, "e820_basic: expected 1 region");
    TEST_ASSERT(ctx.last_start == 0x100000, "e820_basic: wrong start");
    TEST_ASSERT(ctx.last_len == 0x1000000, "e820_basic: wrong length");
    TEST_PASS("e820_basic_valid_entry");
}

/* Test: Zero-length entry is skipped */
static void test_e820_zero_length_skipped(void) {
    e820_entry_t map[2] = {
        { .addr = 0x100000, .len = 0, .type = E820_USABLE },  /* Zero */
        { .addr = 0x200000, .len = 0x100000, .type = E820_USABLE }
    };
    
    struct e820_test_ctx ctx = {0};
    pmm_walk_e820(map, 2, e820_test_cb, &ctx);
    
    TEST_ASSERT(ctx.region_count == 1, "e820_zero: expected 1 region");
    TEST_ASSERT(ctx.last_start == 0x200000, "e820_zero: wrong region");
    TEST_PASS("e820_zero_length_skipped");
}

/* Test: Reserved type is skipped */
static void test_e820_reserved_skipped(void) {
    e820_entry_t map[2] = {
        { .addr = 0x100000, .len = 0x100000, .type = E820_RESERVED },
        { .addr = 0x200000, .len = 0x100000, .type = E820_USABLE }
    };
    
    struct e820_test_ctx ctx = {0};
    pmm_walk_e820(map, 2, e820_test_cb, &ctx);
    
    TEST_ASSERT(ctx.region_count == 1, "e820_reserved: expected 1");
    TEST_ASSERT(ctx.last_start == 0x200000, "e820_reserved: wrong region");
    TEST_PASS("e820_reserved_skipped");
}

/* Test: ACPI type is included (reclaimable) */
static void test_e820_acpi_included(void) {
    e820_entry_t map[1] = {
        { .addr = 0x100000, .len = 0x10000, .type = E820_ACPI }
    };
    
    struct e820_test_ctx ctx = {0};
    pmm_walk_e820(map, 1, e820_test_cb, &ctx);
    
    TEST_ASSERT(ctx.region_count == 1, "e820_acpi: expected 1 region");
    TEST_PASS("e820_acpi_included");
}

/* Test: NVS type is skipped */
static void test_e820_nvs_skipped(void) {
    e820_entry_t map[1] = {
        { .addr = 0x100000, .len = 0x10000, .type = E820_NVS }
    };
    
    struct e820_test_ctx ctx = {0};
    pmm_walk_e820(map, 1, e820_test_cb, &ctx);
    
    TEST_ASSERT(ctx.region_count == 0, "e820_nvs: expected 0 regions");
    TEST_PASS("e820_nvs_skipped");
}

/* Test: BAD type is skipped */
static void test_e820_bad_skipped(void) {
    e820_entry_t map[1] = {
        { .addr = 0x100000, .len = 0x10000, .type = E820_BAD }
    };
    
    struct e820_test_ctx ctx = {0};
    pmm_walk_e820(map, 1, e820_test_cb, &ctx);
    
    TEST_ASSERT(ctx.region_count == 0, "e820_bad: expected 0 regions");
    TEST_PASS("e820_bad_skipped");
}

/* Test: Address above 4GB is skipped */
static void test_e820_above_4gb_skipped(void) {
    e820_entry_t map[1] = {
        { .addr = 0x100000000ULL, .len = 0x100000, .type = E820_USABLE }
    };
    
    struct e820_test_ctx ctx = {0};
    pmm_walk_e820(map, 1, e820_test_cb, &ctx);
    
    TEST_ASSERT(ctx.region_count == 0, "e820_4gb: expected 0 regions");
    TEST_PASS("e820_above_4gb_skipped");
}

/* Test: Address spanning 4GB is clamped */
static void test_e820_spanning_4gb_clamped(void) {
    e820_entry_t map[1] = {
        { .addr = 0xF0000000ULL, .len = 0x20000000ULL, .type = E820_USABLE }
    };
    
    struct e820_test_ctx ctx = {0};
    pmm_walk_e820(map, 1, e820_test_cb, &ctx);
    
    TEST_ASSERT(ctx.region_count == 1, "e820_span: expected 1 region");
    TEST_ASSERT(ctx.last_start == 0xF0000000, "e820_span: wrong start");
    uint32_t expected_len = 0xFFFFFFFF - 0xF0000000;
    TEST_ASSERT(ctx.last_len == expected_len, "e820_span: not clamped");
    TEST_PASS("e820_spanning_4gb_clamped");
}

/* Test: Empty map (zero count) */
static void test_e820_empty_map(void) {
    struct e820_test_ctx ctx = {0};
    pmm_walk_e820(NULL, 0, e820_test_cb, &ctx);
    
    TEST_ASSERT(ctx.region_count == 0, "e820_empty: expected 0");
    TEST_PASS("e820_empty_map");
}

/* Test: NULL callback is safe */
static void test_e820_null_callback_safe(void) {
    e820_entry_t map[1] = {
        { .addr = 0x100000, .len = 0x100000, .type = E820_USABLE }
    };
    
    /* Should not crash with NULL callback */
    pmm_walk_e820(map, 1, NULL, NULL);
    TEST_PASS("e820_null_callback_safe");
}

/* Test: Multiple entries of mixed types */
static void test_e820_mixed_types(void) {
    e820_entry_t map[5] = {
        { .addr = 0x000000, .len = 0x100000, .type = E820_RESERVED },  /* Skip */
        { .addr = 0x100000, .len = 0x100000, .type = E820_USABLE },    /* Use */
        { .addr = 0x200000, .len = 0x10000,  .type = E820_ACPI },      /* Use */
        { .addr = 0x210000, .len = 0x1000,   .type = E820_NVS },       /* Skip */
        { .addr = 0x300000, .len = 0x800000, .type = E820_USABLE }     /* Use */
    };
    
    struct e820_test_ctx ctx = {0};
    pmm_walk_e820(map, 5, e820_test_cb, &ctx);
    
    TEST_ASSERT(ctx.region_count == 3, "e820_mixed: expected 3 regions");
    TEST_PASS("e820_mixed_types");
}

/* Main test entry point */
void test_e820_parsing(void) {
    kprint("=== E820 Memory Map Parsing Tests ===\n");
    
    test_e820_basic_valid_entry();
    test_e820_zero_length_skipped();
    test_e820_reserved_skipped();
    test_e820_acpi_included();
    test_e820_nvs_skipped();
    test_e820_bad_skipped();
    test_e820_above_4gb_skipped();
    test_e820_spanning_4gb_clamped();
    test_e820_empty_map();
    test_e820_null_callback_safe();
    test_e820_mixed_types();
    
    char buf[64];
    sprintf(buf, "=== e820 tests: %d passed, %d failed ===\n", passed, failed);
    kprint(buf);
}
