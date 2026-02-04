/*
 * Unit tests for vm_map subsystem
 */

#include <stdint.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <arch/i386/pmap.h>
#include <kern/console.h>
#include <stdio.h>

static int tests_passed = 0;
static int tests_failed = 0;

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

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
    
    int ret = vm_map_insert(map, obj, 0, 0x10000, 0x12000, 7, 7, 1);
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
    vm_map_insert(map, NULL, 0, 0x1000, 0x5000, 7, 7, 1);
    
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
    
    vm_map_insert(map, NULL, 0, 0x10000, 0x20000, 7, 7, 1);
    TEST_ASSERT(map->nentries == 1, "one entry after insert");
    
    int ret = vm_map_remove(map, 0x10000, 0x20000);
    TEST_ASSERT(ret == 0, "remove succeeded");
    TEST_ASSERT(map->nentries == 0, "no entries after remove");
    
    vm_map_destroy(map);
    kprint("  PASS\n");
}

// Test 5: Entry flags and inheritance
void test_vm_map_entry_flags(void) {
    kprint("Test: vm_map_entry flags\n");
    
    pmap_t pmap = pmap_create();
    vm_map_t *map = vm_map_create(pmap, 0x1000, 0x100000);
    
    vm_object_t *obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, 0x2000);
    vm_map_insert(map, obj, 0, 0x10000, 0x12000, 7, 7, 1);
    
    vm_map_entry_t *entry = vm_map_lookup(map, 0x10000);
    TEST_ASSERT(entry != NULL, "entry found");
    
    // Set protection
    entry->protection = VM_PROT_READ | VM_PROT_WRITE;
    entry->max_protection = VM_PROT_ALL;
    entry->inheritance = VM_INHERIT_COPY;
    
    TEST_ASSERT(entry->inheritance == VM_INHERIT_COPY, "inheritance set");
    
    vm_map_destroy(map);
    kprint("  PASS\n");
}

// Test 6: Wire/unwire
void test_vm_map_wire(void) {
    kprint("Test: vm_map wire/unwire\n");
    
    pmap_t pmap = pmap_create();
    vm_map_t *map = vm_map_create(pmap, 0x1000, 0x100000);
    vm_map_insert(map, NULL, 0, 0x10000, 0x20000, 7, 7, 1);
    
    int ret = vm_map_wire(map, 0x10000, 0x20000);
    TEST_ASSERT(ret == 0, "wire succeeded");
    
    vm_map_entry_t *entry = vm_map_lookup(map, 0x10000);
    TEST_ASSERT(entry != NULL, "entry found");
    TEST_ASSERT(entry->wire_count == 1, "wire_count is 1");
    TEST_ASSERT((entry->flags & VME_WIRED) != 0, "VME_WIRED set");
    
    vm_map_unwire(map, 0x10000, 0x20000);
    TEST_ASSERT(entry->wire_count == 0, "wire_count is 0 after unwire");
    TEST_ASSERT((entry->flags & VME_WIRED) == 0, "VME_WIRED cleared");
    
    vm_map_destroy(map);
    kprint("  PASS\n");
}

void test_vm_map_benchmark(void) {
    kprint("Test: vm_map benchmark (Linear Lookup)\n");

    pmap_t pmap = pmap_create();
    vm_map_t *map = vm_map_create(pmap, 0x1000, 0xFFFFFFFF);

    // 1. Populate Map with 2000 entries
    int entries = 2000;
    uintptr_t start = 0x10000;
    uintptr_t size = 0x1000; // 4KB

    for (int i = 0; i < entries; i++) {
        vm_object_t *obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, size);
        vm_map_insert(map, obj, 0, start, start + size, 7, 7, 1);
        start += size + 0x1000; // Leave gap to prevent merging if any
    }

    // 2. Perform Lookups
    int iterations = 100; // 100 iterations (reduced from 10k for speed in QEMU)

    // We simulate sequential access pattern which is common
    uint64_t start_cycles = rdtsc();

    for (int j = 0; j < iterations; j++) {
        // Lookup every entry sequentially
        uintptr_t addr = 0x10000;
        for (int i = 0; i < entries; i++) {
            volatile vm_map_entry_t *e = vm_map_lookup(map, addr);
            (void)e;
            addr += size + 0x1000;
        }
    }

    uint64_t end_cycles = rdtsc();
    uint64_t total_cycles = end_cycles - start_cycles;

    char buf[128];
    sprintf(buf, "  Benchmark: %d lookups took %u cycles (avg %u)\n",
            iterations * entries, (uint32_t)total_cycles, (uint32_t)(total_cycles / (iterations * entries)));
    kprint(buf);

    vm_map_destroy(map);
}

void run_vm_map_tests(void) {
    kprint("\n=== VM Map Unit Tests ===\n");
    
    test_vm_map_lifecycle();
    test_vm_map_insert_lookup();
    test_vm_map_find_space();
    test_vm_map_remove();
    test_vm_map_entry_flags();
    test_vm_map_wire();
    test_vm_map_benchmark();
    
    kprint("\nVM Map Tests Complete\n");
}
