/*
 * Unit tests for vm_fault subsystem
 */

#include <stdint.h>
#include <sys/types.h>
#include <vm/vm_fault.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_pager.h>
#include <vfs/vfs.h>
#include <arch/i386/pmap.h>
#include <kern/console.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int fake_file_read_calls = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        kprint("FAIL: "); kprint(msg); kprint("\n"); \
        tests_failed++; \
        return; \
    } \
    tests_passed++; \
} while(0)

static size_t fake_file_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    uint8_t *file_data = (uint8_t *)(uintptr_t)node->impl;
    fake_file_read_calls++;

    for (size_t i = 0; i < size; i++) {
        buffer[i] = file_data[offset + i];
    }

    return size;
}

// Test 1: Simple allocate-on-demand fault
void test_vm_fault_simple(void) {
    kprint("Test: vm_fault simple allocation\n");
    
    pmap_t pmap = pmap_create();
    vm_map_t *map = vm_map_create(pmap, 0x1000, 0x100000);
    vm_object_t *obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, 0x2000);

    pmap_activate(pmap);
    
    vm_map_insert(map, obj, 0, 0x10000, 0x12000, 7, 7, 1);
    
    // Simulate read fault
    int ret = vm_fault(map, 0x10000, VM_PROT_READ);
    TEST_ASSERT(ret == VM_FAULT_SUCCESS, "read fault success");
    
    // Verify page exists in object
    vm_page_t *p = vm_object_lookup_page(obj, 0);
    TEST_ASSERT(p != NULL, "page allocated in object");
    TEST_ASSERT(p->flags & PG_VALID, "page is valid");
    
    // Check pmap mapping
    uintptr_t pa = pmap_extract(pmap, 0x10000);
    TEST_ASSERT(pa == p->phys_addr, "pmap mapped correctly");
    
    pmap_activate(pmap_kernel());
    vm_map_destroy(map);
    kprint("  PASS\n");
}

// Test 2: Copy-on-Write fault
void test_vm_fault_cow(void) {
    kprint("Test: vm_fault COW\n");
    
    pmap_t pmap = pmap_create();
    vm_map_t *map = vm_map_create(pmap, 0x1000, 0x100000);
    vm_object_t *obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, 0x1000);

    pmap_activate(pmap);
    
    // 1. Populate initial page
    vm_map_insert(map, obj, 0, 0x10000, 0x11000, 7, 7, 1);
    vm_fault(map, 0x10000, VM_PROT_WRITE); // Allocate writeable page
    vm_page_t *original_page = vm_object_lookup_page(obj, 0);
    
    // 2. Create shadow (simulate fork)
    vm_object_t *shadow = vm_object_shadow(obj);
    
    // 3. Remap with shadow object
    vm_map_remove(map, 0x10000, 0x11000);
    vm_map_insert(map, shadow, 0, 0x10000, 0x11000, 5, 7, 1);
    
    // 4. Read fault should map original page (Read-Only)
    // Note: pmap_extract permissions check not easily available here, assuming pmap handled it
    int ret = vm_fault(map, 0x10000, VM_PROT_READ);
    TEST_ASSERT(ret == VM_FAULT_SUCCESS, "cow read fault success");
    
    uintptr_t pa_read = pmap_extract(pmap, 0x10000);
    TEST_ASSERT(pa_read == original_page->phys_addr, "cow read maps original page");
    
    // 5. Write fault should trigger copy
    ret = vm_fault(map, 0x10000, VM_PROT_WRITE);
    TEST_ASSERT(ret == VM_FAULT_SUCCESS, "cow write fault success");
    
    uintptr_t pa_write = pmap_extract(pmap, 0x10000);
    TEST_ASSERT(pa_write != original_page->phys_addr, "cow write maps NEW page");
    
    pmap_activate(pmap_kernel());
    vm_map_destroy(map);
    kprint("  PASS\n");
}

void test_vm_fault_file_backed(void) {
    kprint("Test: vm_fault file-backed pager\n");

    static uint8_t file_page[4096];
    fs_node_t fake_file = {0};
    pmap_t pmap = pmap_create();
    vm_map_t *map = vm_map_create(pmap, 0x1000, 0x100000);
    vm_object_t *obj = vm_object_allocate(VM_OBJ_TYPE_VNODE, 0x1000);

    for (int i = 0; i < 4096; i++) {
        file_page[i] = (uint8_t)(i & 0xFF);
    }
    fake_file_read_calls = 0;

    fake_file.impl = (uintptr_t)file_page;
    fake_file.read = fake_file_read;
    obj->pager = vm_pager_allocate(VM_OBJ_TYPE_VNODE, &fake_file, 0x1000, VM_PROT_READ, 0);

    pmap_activate(pmap);

    TEST_ASSERT(obj->pager != NULL, "pager allocated");
    TEST_ASSERT(vm_map_insert(map, obj, 0, 0x14000, 0x15000, VM_PROT_READ, VM_PROT_READ, VM_INHERIT_COPY) == 0,
                "map insert succeeded");
    TEST_ASSERT(vm_fault(map, 0x14000, VM_PROT_READ) == VM_FAULT_SUCCESS, "file-backed fault success");

    vm_page_t *page = vm_object_lookup_page(obj, 0);
    TEST_ASSERT(page != NULL, "file-backed page present");
    TEST_ASSERT(page->flags & PG_VALID, "file-backed page valid");
    TEST_ASSERT(fake_file_read_calls > 0, "vnode pager used VFS read path");
    TEST_ASSERT(*(volatile uint8_t *)0x14000 == file_page[0], "mapped data byte 0 matches");
    TEST_ASSERT(*(volatile uint8_t *)(0x14000 + 255) == file_page[255], "mapped data byte 255 matches");

    pmap_activate(pmap_kernel());
    vm_map_destroy(map);
    kprint("  PASS\n");
}

void run_vm_fault_tests(void) {
    kprint("\n=== VM Fault Unit Tests ===\n");
    test_vm_fault_simple();
    test_vm_fault_cow();
    test_vm_fault_file_backed();
    kprint("\nVM Fault Tests Complete\n");
}
