/*
 * test_pmm_watermark.c - Unit tests for PMM bootstrap watermark allocator
 */

#include <stdint.h>
#include <stdio.h>
#include <kern/console.h>
#include <arch/i386/pmm.h>

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

static uintptr_t test_phys(void *vaddr) {
    return (uintptr_t)vaddr - 0xC0000000U;
}

static void test_watermark_sequential(void) {
    pmm_watermark_init(0x00200000, 0x00208000); /* 32KB */

    void *a = pmm_watermark_alloc(64, 16);
    void *b = pmm_watermark_alloc(64, 16);

    TEST_ASSERT(a != NULL, "watermark_sequential: first alloc failed");
    TEST_ASSERT(b != NULL, "watermark_sequential: second alloc failed");
    TEST_ASSERT(test_phys(b) >= test_phys(a) + 64, "watermark_sequential: non-monotonic allocation");
    TEST_PASS("watermark_sequential");
}

static void test_watermark_alignment(void) {
    pmm_watermark_init(0x00200003, 0x00210000);

    void *a = pmm_watermark_alloc(1, 4096);
    void *b = pmm_watermark_alloc(1, 64);

    TEST_ASSERT(a != NULL, "watermark_alignment: first alloc failed");
    TEST_ASSERT(b != NULL, "watermark_alignment: second alloc failed");
    TEST_ASSERT((test_phys(a) & 0xFFF) == 0, "watermark_alignment: 4KB alignment failed");
    TEST_ASSERT((test_phys(b) & 0x3F) == 0, "watermark_alignment: 64-byte alignment failed");
    TEST_PASS("watermark_alignment");
}

static void test_watermark_exhaustion(void) {
    pmm_watermark_init(0x00300000, 0x00302000); /* 8KB */

    void *a = pmm_watermark_alloc(4096, 4096);
    void *b = pmm_watermark_alloc(4096, 4096);
    void *c = pmm_watermark_alloc(4096, 4096);

    TEST_ASSERT(a != NULL, "watermark_exhaustion: first alloc failed");
    TEST_ASSERT(b != NULL, "watermark_exhaustion: second alloc failed");
    TEST_ASSERT(c == NULL, "watermark_exhaustion: expected NULL on exhaustion");
    TEST_PASS("watermark_exhaustion");
}

static void test_watermark_used_counter(void) {
    pmm_watermark_init(0x00400000, 0x00410000);
    (void)pmm_watermark_alloc(128, 16);
    (void)pmm_watermark_alloc(256, 64);

    uint32_t used = pmm_watermark_used();
    TEST_ASSERT(used >= 384, "watermark_used: used counter too small");
    TEST_ASSERT(used < 0x10000, "watermark_used: used counter exceeds range");
    TEST_PASS("watermark_used_counter");
}

static void test_watermark_lowmem_clamp(void) {
    pmm_watermark_init(0x00F00000, 0x02000000); /* End should clamp to 16MB */

    void *a = pmm_watermark_alloc(0x100000, 4096);
    void *b = pmm_watermark_alloc(4096, 4096);

    TEST_ASSERT(a != NULL, "watermark_lowmem_clamp: first alloc failed");
    TEST_ASSERT(b == NULL, "watermark_lowmem_clamp: expected clamp exhaustion");
    TEST_PASS("watermark_lowmem_clamp");
}

void test_pmm_watermark(void) {
    kprint("=== PMM Watermark Allocator Tests ===\n");

    test_watermark_sequential();
    test_watermark_alignment();
    test_watermark_exhaustion();
    test_watermark_used_counter();
    test_watermark_lowmem_clamp();

    char buf[80];
    sprintf(buf, "=== watermark tests: %d passed, %d failed ===\n", passed, failed);
    kprint(buf);
}
