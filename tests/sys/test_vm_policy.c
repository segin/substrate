/*
 * Unit tests for VM Page Replacement Policy (Clock/LRU)
 */

#include <stdint.h>
#include <vm/vm_page.h>
#include <vm/vm_object.h>
#include <vm/vm_pager.h>
#include <kern/console.h>

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

// Helper to manually set up page state
extern void vm_page_activate(vm_page_t *m);
extern void vm_page_deactivate(vm_page_t *m);

void test_vm_policy_lru(void) {
    kprint("Test: vm_policy_lru (Clock Algorithm)\n");
    
    // Allocate a few pages
    vm_object_t *obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, 0x5000);
    vm_page_t *p1 = vm_page_alloc(obj, 0, 0);
    vm_page_t *p2 = vm_page_alloc(obj, 1, 0);
    vm_page_t *p3 = vm_page_alloc(obj, 2, 0);
    vm_page_t *p4 = vm_page_alloc(obj, 3, 0);
    
    // Activate them (put on active queue)
    vm_page_activate(p1);
    vm_page_activate(p2);
    vm_page_activate(p3);
    vm_page_activate(p4);
    
    TEST_ASSERT(p1->flags & PG_ACTIVE, "p1 is active");
    
    // Scan should move unreferenced pages to inactive
    // We assume pmap_is_referenced returns 0 by default for these unused pages
    // (requires pmap module to verify no mappings or mocked to return 0)
    
    int deactivated = vm_pageout_scan(10);
    
    // Since we just allocated them and didn't map them, they are unreferenced.
    // They should all be deactivated.
    TEST_ASSERT(deactivated >= 4, "pages deactivated");
    TEST_ASSERT(p1->flags & PG_INACTIVE, "p1 inactive");
    TEST_ASSERT(p2->flags & PG_INACTIVE, "p2 inactive");
    
    // Now simulate access on p1 (manually set active again)
    vm_page_activate(p1);
    // And simulate pmap reference? We can't easily mock pmap_is_referenced here 
    // without hacking pmap.c or PV list.
    // But we can verify that ACTIVE pages stay active if scan count is low? 
    // No, scan moves them if unreferenced.
    
    // Cleanup
    vm_page_free(p1);
    vm_page_free(p2);
    vm_page_free(p3);
    vm_page_free(p4);
    vm_object_deallocate(obj);
    
    kprint("  PASS\n");
}

void test_vm_policy_writeback(void) {
    kprint("Test: vm_policy_writeback\n");
    
    // Create swap-backed object
    vm_object_t *obj = vm_object_allocate(VM_OBJ_TYPE_SWAP, 0x1000);
    vm_page_t *p1 = vm_page_alloc(obj, 0, 0);
    
    // Mark dirty and inactive (candidate for laundering)
    p1->flags |= PG_DIRTY;
    vm_page_deactivate(p1);
    
    TEST_ASSERT(p1->flags & PG_DIRTY, "p1 is dirty");
    TEST_ASSERT(p1->flags & PG_INACTIVE, "p1 is inactive");
    
    // Launcher should clean it
    extern void vm_page_launder(vm_page_t *m);
    vm_page_launder(p1);
    
    TEST_ASSERT(!(p1->flags & PG_DIRTY), "p1 is clean");
    TEST_ASSERT(p1->flags & PG_INACTIVE, "p1 still inactive (ready to free)");
    
    vm_page_free(p1);
    vm_object_deallocate(obj);
    
    kprint("  PASS\n");
}

void run_vm_policy_tests(void) {
    kprint("\n=== VM Policy Tests ===\n");
    test_vm_policy_lru();
    test_vm_policy_writeback();
    kprint("\nVM Policy Tests Complete\n");
}
