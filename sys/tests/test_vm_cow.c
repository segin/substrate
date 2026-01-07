/*
 * Unit tests for CoW subsystem (vm_map_fork)
 */

#include <stdint.h>
#include "../vm/vm_map.h"
#include "../vm/vm_object.h"
#include "../arch/i386/pmap.h"
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

void test_vm_map_fork_cow(void) {
    kprint("Test: vm_map_fork (COW)\n");
    
    pmap_t parent_pmap = pmap_create();
    vm_map_t *parent_map = vm_map_create(parent_pmap, 0x1000, 0x100000);
    
    vm_object_t *obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, 0x1000);
    vm_map_insert(parent_map, obj, 0, 0x10000, 0x11000);
    
    // Set to Copy-on-Write
    vm_map_entry_t *entry = vm_map_lookup(parent_map, 0x10000);
    entry->inheritance = VM_INHERIT_COPY;
    entry->protection = VM_PROT_READ | VM_PROT_WRITE;
    
    // Fork
    pmap_t child_pmap = pmap_create();
    vm_map_t *child_map = vm_map_fork(parent_map, child_pmap);
    TEST_ASSERT(child_map != NULL, "fork succeeded");
    
    // Check Child Entry
    vm_map_entry_t *child_entry = vm_map_lookup(child_map, 0x10000);
    TEST_ASSERT(child_entry != NULL, "child has entry");
    TEST_ASSERT(child_entry->object == obj, "child shares object");
    TEST_ASSERT(obj->ref_count == 2, "object refcount 2");
    
    // Check flags
    TEST_ASSERT(obj->flags & VM_OBJ_COPY, "object marked COPY");
    
    // Cleanup
    vm_map_destroy(child_map);
    TEST_ASSERT(obj->ref_count == 1, "refcount back to 1");
    
    vm_map_destroy(parent_map);
    kprint("  PASS\n");
}

void run_vm_cow_tests(void) {
    kprint("\n=== VM Copy-on-Write Tests ===\n");
    test_vm_map_fork_cow();
    kprint("\nVM CoW Tests Complete\n");
}
