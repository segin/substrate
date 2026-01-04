/*
 * Unit tests for pmap_create/destroy
 */

#include "../arch/i386/pmap.h"
#include "../arch/i386/pmm.h"
#include "../kern/console.h"
#include <stdint.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        kprint("FAIL: "); kprint(msg); kprint("\n"); \
        tests_failed++; \
        return; \
    } \
    tests_passed++; \
} while(0)

// Test 1: Create and destroy pmap lifecycle
void test_pmap_lifecycle(void) {
    kprint("Test: pmap lifecycle\n");
    
    pmap_t pmap = pmap_create();
    TEST_ASSERT(pmap != 0, "pmap_create returned non-NULL");
    
    // Validate it's a proper physical address
    uint32_t phys = (uint32_t)pmap;
    TEST_ASSERT((phys & 0xFFF) == 0, "pmap is page-aligned");
    
    // Destroy it
    pmap_destroy(pmap);
    
    kprint("  PASS\n");
}

// Test 2: Multiple pmaps can coexist
void test_multiple_pmaps(void) {
    kprint("Test: multiple pmaps\n");
    
    pmap_t pmap1 = pmap_create();
    pmap_t pmap2 = pmap_create();
    pmap_t pmap3 = pmap_create();
    
    TEST_ASSERT(pmap1 != 0, "pmap1 created");
    TEST_ASSERT(pmap2 != 0, "pmap2 created");
    TEST_ASSERT(pmap3 != 0, "pmap3 created");
    
    TEST_ASSERT(pmap1 != pmap2, "pmaps have different addresses");
    TEST_ASSERT(pmap2 != pmap3, "pmaps have different addresses");
    
    pmap_destroy(pmap1);
    pmap_destroy(pmap2);
    pmap_destroy(pmap3);
    
    kprint("  PASS\n");
}

// Test 3: Cannot destroy kernel pmap
void test_kernel_pmap_protection(void) {
    kprint("Test: kernel pmap protection\n");
    
    pmap_t kernel = pmap_kernel();
    TEST_ASSERT(kernel != 0, "kernel pmap exists");
    
    // This should be a no-op
    pmap_destroy(kernel);
    
    // Kernel should still be valid
    TEST_ASSERT(pmap_kernel() == kernel, "kernel pmap unchanged");
    
    kprint("  PASS\n");
}

// Test 4: NULL pmap handling
void test_null_pmap(void) {
    kprint("Test: NULL pmap handling\n");
    
    // Should not crash
    pmap_destroy(0);
    
    kprint("  PASS\n");
}

// Check for memory leaks
static void test_memory_leak(void) {
    kprint("Test: Memory Leak Check... ");
    
    extern size_t pmm_get_used_blocks(void);
    size_t start_blocks = pmm_get_used_blocks();
    
    // Create and destroy 10 pmaps
    for (int i = 0; i < 10; i++) {
        pmap_t pmap = pmap_create();
        TEST_ASSERT(pmap != 0, "pmap created");
        pmap_destroy(pmap);
    }
    
    size_t final_blocks = pmm_get_used_blocks();
    
    // Should have same number of used blocks (no leak)
    TEST_ASSERT(start_blocks == final_blocks, "no memory leak detected");
    
    kprint("  PASS\n");
}

void run_pmap_tests(void) {
    kprint("\n=== PMAP Unit Tests ===\n");
    
    test_pmap_lifecycle();
    test_multiple_pmaps();
    test_kernel_pmap_protection();
    test_null_pmap();
    test_memory_leak();
    
    kprint("\nResults: ");
    kprint("Passed: ");
    // TODO: Add itoa to print numbers
    kprint(" Failed: ");
    kprint("\n");
}
