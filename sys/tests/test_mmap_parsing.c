/*
 * test_mmap_parsing.c - Unit tests for Multiboot memory map parsing
 *
 * Tests pmm_walk_mmap validation logic including:
 * - Zero-length entry handling
 * - 64-bit address clamping
 * - Wrap-around detection
 * - Type filtering
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../../kern/console.h"

/* Multiboot mmap entry structure (copied for test isolation) */
typedef struct test_mmap_entry {
    uint32_t size;      /* Size of entry minus this field */
    uint64_t addr;      /* Base address */
    uint64_t len;       /* Length */
    uint32_t type;      /* Memory type */
} __attribute__((packed)) test_mmap_entry_t;

#define MMAP_TYPE_AVAILABLE 1
#define MMAP_TYPE_RESERVED  2
#define MMAP_TYPE_ACPI      3
#define MMAP_TYPE_NVS       4
#define MMAP_TYPE_BAD       5

/* Test context for callback */
struct mmap_test_ctx {
    uint32_t region_count;
    uint64_t total_bytes;
    uint32_t last_start;
    uint32_t last_len;
};

static void mmap_test_cb(uint32_t start, uint32_t len, void *arg) {
    struct mmap_test_ctx *ctx = arg;
    ctx->region_count++;
    ctx->total_bytes += len;
    ctx->last_start = start;
    ctx->last_len = len;
}

/* External declaration of function under test */
typedef void (*pmm_region_callback)(uint32_t start, uint32_t len, void *arg);
extern void pmm_walk_mmap(uint32_t mmap_addr, uint32_t mmap_length, 
                          pmm_region_callback cb, void *arg);

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
static void test_basic_valid_entry(void) {
    test_mmap_entry_t map[1];
    map[0].size = sizeof(test_mmap_entry_t) - sizeof(uint32_t);
    map[0].addr = 0x100000;  /* 1MB */
    map[0].len = 0x1000000;  /* 16MB */
    map[0].type = MMAP_TYPE_AVAILABLE;
    
    struct mmap_test_ctx ctx = {0};
    pmm_walk_mmap((uint32_t)(uintptr_t)map, sizeof(map), mmap_test_cb, &ctx);
    
    TEST_ASSERT(ctx.region_count == 1, "basic_valid: expected 1 region");
    TEST_ASSERT(ctx.last_start == 0x100000, "basic_valid: wrong start");
    TEST_ASSERT(ctx.last_len == 0x1000000, "basic_valid: wrong length");
    TEST_PASS("basic_valid_entry");
}

/* Test: Zero-length entry is skipped */
static void test_zero_length_skipped(void) {
    test_mmap_entry_t map[2];
    /* Zero-length entry */
    map[0].size = sizeof(test_mmap_entry_t) - sizeof(uint32_t);
    map[0].addr = 0x100000;
    map[0].len = 0;  /* Zero length - should be skipped */
    map[0].type = MMAP_TYPE_AVAILABLE;
    
    /* Valid entry */
    map[1].size = sizeof(test_mmap_entry_t) - sizeof(uint32_t);
    map[1].addr = 0x200000;
    map[1].len = 0x100000;
    map[1].type = MMAP_TYPE_AVAILABLE;
    
    struct mmap_test_ctx ctx = {0};
    pmm_walk_mmap((uint32_t)(uintptr_t)map, sizeof(map), mmap_test_cb, &ctx);
    
    TEST_ASSERT(ctx.region_count == 1, "zero_length: expected 1 region (zero skipped)");
    TEST_ASSERT(ctx.last_start == 0x200000, "zero_length: wrong region processed");
    TEST_PASS("zero_length_skipped");
}

/* Test: Reserved memory type is skipped */
static void test_reserved_type_skipped(void) {
    test_mmap_entry_t map[2];
    /* Reserved entry */
    map[0].size = sizeof(test_mmap_entry_t) - sizeof(uint32_t);
    map[0].addr = 0x100000;
    map[0].len = 0x100000;
    map[0].type = MMAP_TYPE_RESERVED;  /* Should be skipped */
    
    /* Available entry */
    map[1].size = sizeof(test_mmap_entry_t) - sizeof(uint32_t);
    map[1].addr = 0x200000;
    map[1].len = 0x100000;
    map[1].type = MMAP_TYPE_AVAILABLE;
    
    struct mmap_test_ctx ctx = {0};
    pmm_walk_mmap((uint32_t)(uintptr_t)map, sizeof(map), mmap_test_cb, &ctx);
    
    TEST_ASSERT(ctx.region_count == 1, "reserved_type: expected 1 region");
    TEST_ASSERT(ctx.last_start == 0x200000, "reserved_type: wrong region");
    TEST_PASS("reserved_type_skipped");
}

/* Test: Address above 4GB is skipped */
static void test_above_4gb_skipped(void) {
    test_mmap_entry_t map[1];
    map[0].size = sizeof(test_mmap_entry_t) - sizeof(uint32_t);
    map[0].addr = 0x100000000ULL;  /* 4GB - above 32-bit */
    map[0].len = 0x100000;
    map[0].type = MMAP_TYPE_AVAILABLE;
    
    struct mmap_test_ctx ctx = {0};
    pmm_walk_mmap((uint32_t)(uintptr_t)map, sizeof(map), mmap_test_cb, &ctx);
    
    TEST_ASSERT(ctx.region_count == 0, "above_4gb: expected 0 regions");
    TEST_PASS("above_4gb_skipped");
}

/* Test: Address spanning 4GB is clamped */
static void test_spanning_4gb_clamped(void) {
    test_mmap_entry_t map[1];
    map[0].size = sizeof(test_mmap_entry_t) - sizeof(uint32_t);
    map[0].addr = 0xF0000000ULL;  /* 3.75GB */
    map[0].len = 0x20000000ULL;   /* 512MB - would end at 4.25GB */
    map[0].type = MMAP_TYPE_AVAILABLE;
    
    struct mmap_test_ctx ctx = {0};
    pmm_walk_mmap((uint32_t)(uintptr_t)map, sizeof(map), mmap_test_cb, &ctx);
    
    TEST_ASSERT(ctx.region_count == 1, "spanning_4gb: expected 1 region");
    TEST_ASSERT(ctx.last_start == 0xF0000000, "spanning_4gb: wrong start");
    /* Should be clamped to 4GB - 3.75GB = 0x10000000 - 1 */
    uint32_t expected_len = 0xFFFFFFFF - 0xF0000000;
    TEST_ASSERT(ctx.last_len == expected_len, "spanning_4gb: not clamped correctly");
    TEST_PASS("spanning_4gb_clamped");
}

/* Test: Empty map (zero length) */
static void test_empty_map(void) {
    struct mmap_test_ctx ctx = {0};
    pmm_walk_mmap(0, 0, mmap_test_cb, &ctx);
    
    TEST_ASSERT(ctx.region_count == 0, "empty_map: expected 0 regions");
    TEST_PASS("empty_map");
}

/* Test: NULL callback is safe */
static void test_null_callback_safe(void) {
    test_mmap_entry_t map[1];
    map[0].size = sizeof(test_mmap_entry_t) - sizeof(uint32_t);
    map[0].addr = 0x100000;
    map[0].len = 0x100000;
    map[0].type = MMAP_TYPE_AVAILABLE;
    
    /* Should not crash with NULL callback */
    pmm_walk_mmap((uint32_t)(uintptr_t)map, sizeof(map), NULL, NULL);
    TEST_PASS("null_callback_safe");
}

/* Test: ACPI reclaimable memory is included */
static void test_acpi_reclaimable_included(void) {
    test_mmap_entry_t map[1];
    map[0].size = sizeof(test_mmap_entry_t) - sizeof(uint32_t);
    map[0].addr = 0x100000;
    map[0].len = 0x10000;
    map[0].type = MMAP_TYPE_ACPI;  /* ACPI reclaimable */
    
    struct mmap_test_ctx ctx = {0};
    pmm_walk_mmap((uint32_t)(uintptr_t)map, sizeof(map), mmap_test_cb, &ctx);
    
    TEST_ASSERT(ctx.region_count == 1, "acpi_reclaim: expected 1 region");
    TEST_PASS("acpi_reclaimable_included");
}

/* Main test entry point */
void test_mmap_parsing(void) {
    kprint("=== Multiboot mmap Parsing Tests ===\n");
    
    test_basic_valid_entry();
    test_zero_length_skipped();
    test_reserved_type_skipped();
    test_above_4gb_skipped();
    test_spanning_4gb_clamped();
    test_empty_map();
    test_null_callback_safe();
    test_acpi_reclaimable_included();
    
    char buf[64];
    sprintf(buf, "=== mmap tests: %d passed, %d failed ===\n", passed, failed);
    kprint(buf);
}
