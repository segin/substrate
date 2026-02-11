/*
 * Unit tests for x86_64 pmap
 */

#include <arch/x86_64/pmap.h>
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

extern pmap_t pmap_create(void);
extern void pmap_destroy(pmap_t pmap);

void test_pmap_x64_lifecycle(void) {
    kprint("Test: x64 pmap lifecycle\n");
    pmap_t pmap = pmap_create();
    TEST_ASSERT(pmap != NULL, "pmap_create returned non-NULL");
    pmap_destroy(pmap);
    kprint("  PASS\n");
}

void test_pmap_x64_refcount(void) {
    kprint("Test: x64 pmap refcount\n");
    pmap_t pmap = pmap_create();
    TEST_ASSERT(pmap != NULL, "pmap_create");
    TEST_ASSERT(pmap->ref_count == 1, "ref_count init 1");

    // Manually increment
    __sync_fetch_and_add(&pmap->ref_count, 1);
    TEST_ASSERT(pmap->ref_count == 2, "ref_count 2");

    // Destroy should decrement but not destroy
    pmap_destroy(pmap);
    TEST_ASSERT(pmap->ref_count == 1, "ref_count back to 1");

    // Destroy should destroy
    pmap_destroy(pmap);

    kprint("  PASS\n");
}

void test_pmap_x64_enter_extract(void) {
    kprint("Test: x64 pmap_enter/extract\n");
    pmap_t pmap = pmap_kernel(); // Use kernel pmap as it is active
    
    // Virtual address at 256GB mark (USER space)
    uint64_t va = 0x0000004000000000UL; 
    uint64_t pa = 0x1234000;
    
    // Enter mapping
    int ret = pmap_enter(pmap, va, pa, 0x07, 0); // RW User
    TEST_ASSERT(ret == 0, "pmap_enter returned 0");
    
    // Extract
    uint64_t extracted_pa = pmap_extract(pmap, va);
    TEST_ASSERT((extracted_pa & ~0xFFF) == pa, "pmap_extract matches PA");
    
    // Remove
    pmap_remove(pmap, va);
    extracted_pa = pmap_extract(pmap, va);
    TEST_ASSERT(extracted_pa == 0, "pmap_extract returns 0 after remove");

    kprint("  PASS\n");
}

void test_pmap_x64_foreign(void) {
    kprint("Test: x64 foreign pmap modification\n");
    pmap_t pmap = pmap_create();
    TEST_ASSERT(pmap != NULL, "pmap_create");

    // Note: pmap_create does NOT activate the pmap, so it is foreign.
    // Assuming pmap_kernel() is active.
    TEST_ASSERT(pmap != pmap_kernel(), "new pmap is not kernel pmap");

    uint64_t va = 0x0000004000000000UL;
    uint64_t pa = 0x2345000;

    // Enter mapping into inactive pmap
    int ret = pmap_enter(pmap, va, pa, 0x07, 0);
    TEST_ASSERT(ret == 0, "pmap_enter on foreign pmap returned 0");

    // Extract from inactive pmap
    uint64_t extracted_pa = pmap_extract(pmap, va);
    TEST_ASSERT((extracted_pa & ~0xFFF) == pa, "pmap_extract on foreign pmap matches PA");

    // Remove from inactive pmap
    pmap_remove(pmap, va);
    extracted_pa = pmap_extract(pmap, va);
    TEST_ASSERT(extracted_pa == 0, "pmap_extract returns 0 after remove on foreign pmap");

    pmap_destroy(pmap);
    kprint("  PASS\n");
}

void run_pmap_x64_tests(void) {
    kprint("\n=== PMAP x86_64 Unit Tests ===\n");
    test_pmap_x64_lifecycle();
    test_pmap_x64_enter_extract();
    test_pmap_x64_refcount();
    test_pmap_x64_foreign();
    kprint("\nDone.\n");
}
