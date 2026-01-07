/*
 * Unit tests for Pager subsystem
 */

#include <stdint.h>
#include "../vm/vm_pager.h"
#include "../vm/vm_object.h"
#include "../kern/console.h"

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

void test_vm_pager_lifecycle(void) {
    kprint("Test: vm_pager lifecycle\n");
    
    // Create swap pager
    vm_pager_t *pager = vm_pager_allocate(VM_OBJ_TYPE_SWAP, NULL, 0x10000, 0, 0);
    TEST_ASSERT(pager != NULL, "pager allocated");
    TEST_ASSERT(pager->ops == &swap_pager_ops, "swap ops assigned");
    
    vm_pager_deallocate(pager);
    kprint("  PASS\n");
}

void test_vm_pager_io(void) {
    kprint("Test: vm_pager IO (mock)\n");
    
    // Swap pager
    vm_pager_t *pager = vm_pager_allocate(VM_OBJ_TYPE_SWAP, NULL, 0x10000, 0, 0);
    
    vm_page_t page = {0};
    page.pindex = 0;
    
    // Put page
    int ret = vm_pager_put_pages(pager, (vm_page_t **)&page, 1, true);
    TEST_ASSERT(ret == 0, "put_pages success");
    TEST_ASSERT(vm_pager_has_page(pager, 0) == true, "has_page true");
    
    // Get page
    ret = vm_pager_get_pages(pager, (vm_page_t **)&page, 1, true);
    TEST_ASSERT(ret == 0, "get_pages success");
    
    vm_pager_deallocate(pager);
    kprint("  PASS\n");
}

void run_vm_pager_tests(void) {
    kprint("\n=== VM Pager Unit Tests ===\n");
    test_vm_pager_lifecycle();
    test_vm_pager_io();
    kprint("\nVM Pager Tests Complete\n");
}
