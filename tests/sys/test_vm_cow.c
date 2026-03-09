/*
 * Unit tests for CoW subsystem (vm_map_fork)
 */

#include <stdint.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_fault.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/syscall_impl.h>
#include <arch/i386/pmap.h>
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

void test_vm_map_fork_cow(void) {
    kprint("Test: vm_map_fork (COW)\n");
    
    pmap_t parent_pmap = pmap_create();
    vm_map_t *parent_map = vm_map_create(parent_pmap, 0x1000, 0x100000);
    
    vm_object_t *obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, 0x1000);
    vm_map_insert(parent_map, obj, 0, 0x10000, 0x11000, 7, 7, 1);
    
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
    TEST_ASSERT(entry->object != obj, "parent now uses shadow object");
    TEST_ASSERT(child_entry->object != obj, "child now uses shadow object");
    TEST_ASSERT(entry->object != child_entry->object, "parent and child use distinct shadows");
    TEST_ASSERT(entry->object->shadow == obj, "parent shadow points at source");
    TEST_ASSERT(child_entry->object->shadow == obj, "child shadow points at source");
    TEST_ASSERT(obj->flags & VM_OBJ_COPY, "object marked COPY");

    // Cleanup
    vm_map_destroy(child_map);
    TEST_ASSERT(obj->ref_count == 1, "refcount back to 1");
    
    vm_map_destroy(parent_map);
    kprint("  PASS\n");
}

void test_vm_map_fork_mmap_isolation(void) {
    process_t parent_proc = {0};
    process_t child_proc = {0};
    process_t *saved_process = current_process;
    pmap_t parent_pmap = pmap_create();
    pmap_t child_pmap;
    vm_map_t *parent_map = vm_map_create(parent_pmap, 0x1000, 0x100000);
    vm_map_t *child_map;
    void *addr;
    uintptr_t parent_pa_before;
    uintptr_t parent_pa_after;
    uintptr_t child_pa_after;

    kprint("Test: vm_map_fork anonymous mmap isolation\n");

    TEST_ASSERT(parent_pmap != 0, "parent pmap created");
    TEST_ASSERT(parent_map != NULL, "parent map created");

    parent_proc.vm_map = parent_map;
    parent_proc.pmap = (struct pmap *)parent_pmap;
    current_process = &parent_proc;

    pmap_activate(parent_pmap);
    addr = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST_ASSERT(addr != (void *)-1, "anonymous mmap succeeded");
    TEST_ASSERT(vm_fault(parent_map, (uintptr_t)addr, VM_PROT_WRITE) == VM_FAULT_SUCCESS,
                "parent write fault succeeded");
    ((volatile uint8_t *)addr)[0] = 0x12;
    parent_pa_before = pmap_extract(parent_pmap, (uintptr_t)addr);
    TEST_ASSERT(parent_pa_before != 0, "parent physical mapping present");

    child_pmap = pmap_create();
    TEST_ASSERT(child_pmap != 0, "child pmap created");
    child_map = vm_map_fork(parent_map, child_pmap);
    TEST_ASSERT(child_map != NULL, "forked child map");

    child_proc.vm_map = child_map;
    child_proc.pmap = (struct pmap *)child_pmap;
    current_process = &child_proc;
    pmap_activate(child_pmap);

    TEST_ASSERT(vm_fault(child_map, (uintptr_t)addr, VM_PROT_READ) == VM_FAULT_SUCCESS, "child read fault succeeded");
    TEST_ASSERT(*(volatile uint8_t *)addr == 0x12, "child sees inherited data");
    TEST_ASSERT(vm_fault(child_map, (uintptr_t)addr, VM_PROT_WRITE) == VM_FAULT_SUCCESS, "child write fault succeeded");
    *(volatile uint8_t *)addr = 0x34;
    child_pa_after = pmap_extract(child_pmap, (uintptr_t)addr);
    TEST_ASSERT(child_pa_after != 0, "child physical mapping present after write");

    current_process = &parent_proc;
    pmap_activate(parent_pmap);
    TEST_ASSERT(vm_fault(parent_map, (uintptr_t)addr, VM_PROT_READ) == VM_FAULT_SUCCESS, "parent refault succeeded");
    TEST_ASSERT(*(volatile uint8_t *)addr == 0x12, "parent retained original data");
    parent_pa_after = pmap_extract(parent_pmap, (uintptr_t)addr);
    TEST_ASSERT(parent_pa_after != 0, "parent physical mapping present after refault");
    TEST_ASSERT(parent_pa_after != child_pa_after, "parent and child diverged after child write");

    pmap_activate(pmap_kernel());
    current_process = saved_process;
    vm_map_destroy(child_map);
    vm_map_destroy(parent_map);
    kprint("  PASS\n");
}

void run_vm_cow_tests(void) {
    kprint("\n=== VM Copy-on-Write Tests ===\n");
    test_vm_map_fork_cow();
    test_vm_map_fork_mmap_isolation();
    kprint("\nVM CoW Tests Complete\n");
}
