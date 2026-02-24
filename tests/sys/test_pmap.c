/*
 * Unit tests for pmap_create/destroy
 */

#include <arch/i386/pmap.h>
#include <arch/i386/pmm.h>
#include <kern/console.h>
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
    uint32_t phys = (uintptr_t)pmap;
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
// Test 5: PSE 4MB Page Support
void test_pmap_pse(void) {
    kprint("Test: PSE 4MB Mapping\n");
    pmap_t pmap = pmap_create();
    TEST_ASSERT(pmap != 0, "pmap created");

    // Try valid 4MB alignment
    uint32_t va = 0x800000; // 8MB
    uint32_t pa = 0x400000; // 4MB
    int ret = pmap_enter_pse(pmap, va, pa, PTE_W | PTE_U);
    TEST_ASSERT(ret == 0, "pmap_enter_pse valid alignment");

    // Try invalid alignment
    ret = pmap_enter_pse(pmap, va + 0x1000, pa, 0);
    TEST_ASSERT(ret != 0, "pmap_enter_pse invalid VA alignment");
    
    ret = pmap_enter_pse(pmap, va, pa + 0x1000, 0);
    TEST_ASSERT(ret != 0, "pmap_enter_pse invalid PA alignment");

    pmap_destroy(pmap);
    kprint("  PASS\n");
}

// Test 6: pmap_check consistency check
void test_pmap_check(void) {
    kprint("Test: pmap_check consistency\n");
    pmap_t pmap = pmap_create();
    TEST_ASSERT(pmap != 0, "pmap created");

    int ret = pmap_check(pmap);
    TEST_ASSERT(ret == 0, "pmap_check returns 0 for valid pmap");

    pmap_destroy(pmap);
    kprint("  PASS\n");
}

// Test 7: pmap_dump smoke test (just verify no crash)
void test_pmap_dump(void) {
    kprint("Test: pmap_dump smoke test\n");
    pmap_t pmap = pmap_create();
    TEST_ASSERT(pmap != 0, "pmap created");

    pmap_dump(pmap);  // Should not crash

    pmap_destroy(pmap);
    kprint("  PASS\n");
}

// Test 8: PGE detection via CR4
void test_pge_detection(void) {
    kprint("Test: PGE detection\n");
    
    // Check if PGE is enabled in CR4
    uint32_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    
    int pge_enabled = (cr4 >> 7) & 1;
    if (pge_enabled) {
        kprint("  PGE is enabled in CR4\n");
    } else {
        kprint("  PGE is NOT enabled (may be unsupported CPU)\n");
    }
    
    // Also verify PGE bit via CPUID
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    int has_pge = (edx >> 13) & 1;
    
    if (has_pge && pge_enabled) {
        kprint("  CPUID reports PGE support, CR4.PGE enabled - PASS\n");
    } else if (!has_pge) {
        kprint("  CPUID reports no PGE support - SKIP\n");
    } else {
        kprint("  PGE supported but not enabled - WARN\n");
    }
}

// Test 9: Global page flush function (smoke test)
void test_pge_global_flush(void) {
    kprint("Test: pmap_flush_global_pages\n");
    
    // Just verify it doesn't crash
    pmap_flush_global_pages();
    
    // Verify stats incremented
    struct pmap_stats stats;
    sys_pmap_stats(&stats);
    TEST_ASSERT(stats.tlb_full_flush_count > 0, "tlb_full_flush_count incremented");
    
    kprint("  PASS\n");
}

// Test 10: Replace Page Table with Large Page
void test_pmap_large_replace(void) {
    kprint("Test: pmap_enter_large replace page table\n");

    // Use a high address in kernel space that is likely free (e.g. 0xD0000000)
    uint32_t va = 0xD0000000;

    // 1. Map a 4KB page
    void *page = pmm_alloc_block();
    uint32_t pa = (uint32_t)(uintptr_t)page - 0xC0000000;

    int ret = pmap_enter(pmap_kernel(), va, pa, VM_PROT_READ | VM_PROT_WRITE, 0);
    TEST_ASSERT(ret == 0, "pmap_enter 4KB success");

    // Verify 4KB mapping
    uint32_t extracted_pa = pmap_extract(pmap_kernel(), va);
    TEST_ASSERT(extracted_pa == pa, "pmap_extract matches");

    // 2. Replace with Large Page (using dummy PA 0x20000000)
    uint32_t large_pa = 0x20000000;
    ret = pmap_enter_large(pmap_kernel(), va, large_pa, VM_PROT_READ | VM_PROT_WRITE, 0);

    if (ret == 0) {
        kprint("  pmap_enter_large succeeded\n");

        // Verify it is now a large page
        // pmap_extract should return the new PA
        extracted_pa = pmap_extract(pmap_kernel(), va);
        TEST_ASSERT(extracted_pa == large_pa, "pmap_extract matches large page");

        // Cleanup
        pmap_remove(pmap_kernel(), va);
    } else {
        kprint("  pmap_enter_large failed\n");
        pmap_remove(pmap_kernel(), va);
        TEST_ASSERT(0, "pmap_enter_large failed to replace PT");
    }

    kprint("  PASS\n");
}

static void itoa(int val, char *buf) {
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }

    int is_neg = 0;
    unsigned int uval;

    if (val < 0) {
        is_neg = 1;
        uval = (unsigned int)(-(val + 1)) + 1; // Handle INT_MIN
    } else {
        uval = val;
    }

    char tmp[32];
    int k = 0;
    while (uval > 0) {
        tmp[k++] = (uval % 10) + '0';
        uval /= 10;
    }

    if (is_neg) {
        tmp[k++] = '-';
    }

    // Reverse into buf
    int i = 0;
    while (k > 0) {
        buf[i++] = tmp[--k];
    }
    buf[i] = '\0';
}

void run_pmap_tests(void) {
    char buf[32];

    kprint("\n=== PMAP Unit Tests ===\n");
    
    test_pmap_lifecycle();
    test_multiple_pmaps();
    test_kernel_pmap_protection();
    test_null_pmap();
    test_pmap_pse();
    test_pmap_check();
    test_pmap_dump();
    test_pge_detection();
    test_pge_global_flush();
    test_pmap_large_replace();
    test_memory_leak();
    
    kprint("\nResults: ");
    kprint("Passed: ");
    itoa(tests_passed, buf);
    kprint(buf);
    kprint(" Failed: ");
    itoa(tests_failed, buf);
    kprint(buf);
    kprint("\n");
}
