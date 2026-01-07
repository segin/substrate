/*
 * Unit tests for vm_map subsystem
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

// Test 1: Create and destroy vm_map
void test_vm_map_lifecycle(void) {
    kprint("Test: vm_map lifecycle\n");
    
    pmap_t pmap = pmap_create();
    TEST_ASSERT(pmap != 0, "pmap created");
    
    vm_map_t *map = vm_map_create(pmap, 0x1000, 0xBFFFFFFF);
    TEST_ASSERT(map != NULL, "vm_map_create returned non-NULL");
    TEST_ASSERT(map->nentries == 0, "new map has no entries");
    TEST_ASSERT(map->min_offset == 0x1000, "min_offset correct");
    
    vm_map_destroy(map);
    kprint("  PASS\n");
}

// Test 2: Insert and lookup entries
void test_vm_map_insert_lookup(void) {
    kprint("Test: vm_map insert/lookup\n");
    
    pmap_t pmap = pmap_create();
    vm_map_t *map = vm_map_create(pmap, 0x1000, 0xBFFFFFFF);
    TEST_ASSERT(map != NULL, "map created");
    
    vm_object_t *obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, 0x2000);
    TEST_ASSERT(obj != NULL, "object allocated");
    
    int ret = vm_map_insert(map, obj, 0, 0x10000, 0x12000);
    TEST_ASSERT(ret == 0, "insert succeeded");
    TEST_ASSERT(map->nentries == 1, "one entry");
    
    vm_map_entry_t *entry = vm_map_lookup(map, 0x10500);
    TEST_ASSERT(entry != NULL, "lookup found entry");
    TEST_ASSERT(entry->start == 0x10000, "entry start correct");
    TEST_ASSERT(entry->end == 0x12000, "entry end correct");
    
    entry = vm_map_lookup(map, 0x20000);
    TEST_ASSERT(entry == NULL, "lookup outside range returns NULL");
    
    vm_map_destroy(map);
    kprint("  PASS\n");
}

// Test 3: Find space
void test_vm_map_find_space(void) {
    kprint("Test: vm_map find_space\n");
    
    pmap_t pmap = pmap_create();
    vm_map_t *map = vm_map_create(pmap, 0x1000, 0x100000);
    
    uintptr_t addr;
    int ret = vm_map_find_space(map, &addr, 0x4000);
    TEST_ASSERT(ret == 0, "find_space succeeded");
    TEST_ASSERT(addr == 0x1000, "found at min_offset");
    
    // Insert something and try again
    vm_map_insert(map, NULL, 0, 0x1000, 0x5000);
    
    ret = vm_map_find_space(map, &addr, 0x2000);
    TEST_ASSERT(ret == 0, "find_space after insert");
    TEST_ASSERT(addr == 0x5000, "found after existing entry");
    
    vm_map_destroy(map);
    kprint("  PASS\n");
}

// Test 4: Remove entries
void test_vm_map_remove(void) {
    kprint("Test: vm_map remove\n");
    
    pmap_t pmap = pmap_create();
    vm_map_t *map = vm_map_create(pmap, 0x1000, 0x100000);
    
    vm_map_insert(map, NULL, 0, 0x10000, 0x20000);
    TEST_ASSERT(map->nentries == 1, "one entry after insert");
    
    int ret = vm_map_remove(map, 0x10000, 0x20000);
    TEST_ASSERT(ret == 0, "remove succeeded");
    TEST_ASSERT(map->nentries == 0, "no entries after remove");
    
    vm_map_destroy(map);
    kprint("  PASS\n");
}

void run_vm_map_tests(void) {
    kprint("\n=== VM Map Unit Tests ===\n");
    
    test_vm_map_lifecycle();
    test_vm_map_insert_lookup();
    test_vm_map_find_space();
    test_vm_map_remove();
    
    kprint("\nVM Map Tests Complete\n");
}
