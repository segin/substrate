/*
 * Unit tests for Pager subsystem
 */

#include <stdint.h>
#include <sys/types.h>
#include <vm/vm_pager.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_fault.h>
#include <vm/vm_swap.h>
#include <vfs/vfs.h>
#include <sys/file.h>
#include <arch/i386/pmm.h>
#include <sys/proc.h>
#include <sys/syscall_impl.h>
#include <sys/mman.h>
#include <arch/i386/pmap.h>
#include <string.h>
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

static uint8_t fake_store[4096];
static uint8_t swap_file_buffer[8 * 4096];
static int fake_pager_write_calls = 0;
static int fake_pager_read_calls = 0;

extern struct fs_node *swap_node;
extern process_t *current_process;

static size_t fake_pager_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    uint8_t *store = (uint8_t *)(uintptr_t)node->impl;

    fake_pager_read_calls++;
    memcpy(buffer, store + offset, size);
    return size;
}

static size_t fake_pager_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    uint8_t *store = (uint8_t *)(uintptr_t)node->impl;

    fake_pager_write_calls++;
    memcpy(store + offset, buffer, size);
    return size;
}

static size_t fake_swap_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    if ((size_t)offset + size > sizeof(swap_file_buffer)) {
        return 0;
    }
    memcpy(buffer, swap_file_buffer + offset, size);
    return size;
}

static size_t fake_swap_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node;
    if ((size_t)offset + size > sizeof(swap_file_buffer)) {
        return 0;
    }
    memcpy(swap_file_buffer + offset, buffer, size);
    return size;
}

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

    fs_node_t fake_file = {0};
    vm_object_t *obj = vm_object_allocate(VM_OBJ_TYPE_VNODE, 0x1000);
    vm_page_t *page;
    vm_page_t *pages[1];
    int ret;

    memset(fake_store, 0, sizeof(fake_store));
    fake_file.impl = (uintptr_t)fake_store;
    fake_file.read = fake_pager_read;
    fake_file.write = fake_pager_write;

    vm_pager_t *pager = vm_pager_allocate(VM_OBJ_TYPE_VNODE, &fake_file, 0x1000, 0, 0);
    TEST_ASSERT(pager != NULL, "pager allocated");

    page = vm_page_alloc(obj, 0, 0);
    TEST_ASSERT(page != NULL, "page allocated");
    pages[0] = page;

    memset((void *)(uintptr_t)(page->phys_addr + 0xC0000000), 0x5A, 4096);
    page->flags |= PG_VALID | PG_DIRTY;

    ret = vm_pager_put_pages(pager, pages, 1, true);
    TEST_ASSERT(ret == 0, "put_pages success");
    TEST_ASSERT(vm_pager_has_page(pager, 0) == true, "has_page true");

    memset((void *)(uintptr_t)(page->phys_addr + 0xC0000000), 0, 4096);

    ret = vm_pager_get_pages(pager, pages, 1, true);
    TEST_ASSERT(ret == 0, "get_pages success");
    TEST_ASSERT(((uint8_t *)(uintptr_t)(page->phys_addr + 0xC0000000))[0] == 0x5A, "get_pages restored data");

    vm_page_free(page);
    vm_object_deallocate(obj);
    vm_pager_deallocate(pager);
    kprint("  PASS\n");
}

void test_vm_swap_pager_roundtrip(void) {
    fs_node_t swap_file = {0};
    vm_pager_t *pager;
    vm_object_t *obj;
    vm_page_t *page;
    vm_page_t *pages[1];
    uint64_t total_pages;
    uint64_t free_pages;
    int ret;

    kprint("Test: vm_swap pager round-trip\n");

    memset(swap_file_buffer, 0, sizeof(swap_file_buffer));
    swap_node = NULL;
    swap_file.flags = FS_FILE;
    swap_file.length = sizeof(swap_file_buffer);
    swap_file.read = fake_swap_read;
    swap_file.write = fake_swap_write;

    TEST_ASSERT(vm_swapon(&swap_file) == 0, "swapon succeeded");

    pager = vm_pager_allocate(VM_OBJ_TYPE_SWAP, NULL, 0x2000, 0, 0);
    TEST_ASSERT(pager != NULL, "swap pager allocated");

    obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, 0x1000);
    TEST_ASSERT(obj != NULL, "object allocated");

    page = vm_page_alloc(obj, 0, 0);
    TEST_ASSERT(page != NULL, "page allocated");
    pages[0] = page;

    memset((void *)(uintptr_t)(page->phys_addr + 0xC0000000), 0xA5, 4096);
    page->flags |= PG_VALID | PG_DIRTY;

    ret = vm_pager_put_pages(pager, pages, 1, true);
    TEST_ASSERT(ret == 0, "swap put_pages succeeded");
    TEST_ASSERT(vm_pager_has_page(pager, 0), "swap has page");

    memset((void *)(uintptr_t)(page->phys_addr + 0xC0000000), 0, 4096);
    ret = vm_pager_get_pages(pager, pages, 1, true);
    TEST_ASSERT(ret == 0, "swap get_pages succeeded");
    TEST_ASSERT(((uint8_t *)(uintptr_t)(page->phys_addr + 0xC0000000))[0] == 0xA5,
                "swap restored page data");

    vm_swap_get_stats(&total_pages, &free_pages);
    TEST_ASSERT(total_pages == 8, "swap total pages tracked");
    TEST_ASSERT(free_pages == 7, "swap free pages decremented");

    vm_page_free(page);
    vm_object_deallocate(obj);
    vm_pager_deallocate(pager);

    vm_swap_get_stats(&total_pages, &free_pages);
    TEST_ASSERT(free_pages == 8, "swap blocks freed on pager teardown");
    TEST_ASSERT(vm_swapoff() == 0, "swapoff succeeded");
    kprint("  PASS\n");
}

void test_vm_swap_pager_full(void) {
    fs_node_t swap_file = {0};
    vm_pager_t *pager;
    vm_object_t *obj;
    vm_page_t *page;
    vm_page_t *pages[1];
    int ret;

    kprint("Test: vm_swap pager full condition\n");

    memset(swap_file_buffer, 0, sizeof(swap_file_buffer));
    swap_node = NULL;
    swap_file.flags = FS_FILE;
    swap_file.length = 4 * 4096;
    swap_file.read = fake_swap_read;
    swap_file.write = fake_swap_write;

    TEST_ASSERT(vm_swapon(&swap_file) == 0, "swapon small backing");

    pager = vm_pager_allocate(VM_OBJ_TYPE_SWAP, NULL, 0x8000, 0, 0);
    TEST_ASSERT(pager != NULL, "swap pager allocated");

    obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, 0x1000);
    TEST_ASSERT(obj != NULL, "object allocated");

    page = vm_page_alloc(obj, 0, 0);
    TEST_ASSERT(page != NULL, "page allocated");
    pages[0] = page;

    memset((void *)(uintptr_t)(page->phys_addr + 0xC0000000), 0x3C, 4096);
    page->flags |= PG_VALID | PG_DIRTY;

    for (uint64_t i = 0; i < 4; i++) {
        page->pindex = i;
        ret = vm_pager_put_pages(pager, pages, 1, true);
        TEST_ASSERT(ret == 0, "swap block allocated");
    }

    page->pindex = 4;
    ret = vm_pager_put_pages(pager, pages, 1, true);
    TEST_ASSERT(ret != 0, "swap full reported");

    vm_page_free(page);
    vm_object_deallocate(obj);
    vm_pager_deallocate(pager);
    TEST_ASSERT(vm_swapoff() == 0, "swapoff after full test succeeded");
    kprint("  PASS\n");
}

void test_vm_device_fault_mapping(void) {
    process_t fake_proc = {0};
    process_t *saved_process = current_process;
    pmap_t pmap;
    vm_map_t *map;
    vm_object_t *obj;
    void *device_page;
    uintptr_t device_phys;

    kprint("Test: vm_device pager fault mapping\n");

    device_page = pmm_alloc_block();
    TEST_ASSERT(device_page != NULL, "device backing page allocated");
    device_phys = (uintptr_t)device_page - 0xC0000000;
    memset(device_page, 0xD2, 4096);

    pmap = pmap_create();
    TEST_ASSERT(pmap != 0, "pmap created");
    map = vm_map_create(pmap, 0x1000, 0x100000);
    TEST_ASSERT(map != NULL, "map created");

    fake_proc.vm_map = map;
    fake_proc.pmap = (struct pmap *)pmap;
    current_process = &fake_proc;
    pmap_activate(pmap);

    obj = vm_object_allocate(VM_OBJ_TYPE_DEVICE, 0x1000);
    TEST_ASSERT(obj != NULL, "device object allocated");
    obj->pager = vm_pager_allocate(VM_OBJ_TYPE_DEVICE, (void *)device_phys, 0x1000,
                                   VM_PROT_READ | VM_PROT_WRITE | VM_PROT_USER, 0);
    TEST_ASSERT(obj->pager != NULL, "device pager allocated");
    TEST_ASSERT(vm_pager_has_page(obj->pager, 0), "device pager reports mapped page");

    TEST_ASSERT(vm_map_insert(map, obj, 0, 0x1A000, 0x1B000,
                              VM_PROT_READ | VM_PROT_WRITE | VM_PROT_USER,
                              VM_PROT_READ | VM_PROT_WRITE | VM_PROT_USER,
                              VM_INHERIT_NONE) == 0,
                "device map insert succeeded");

    TEST_ASSERT(vm_fault(map, 0x1A000, VM_PROT_READ) == VM_FAULT_SUCCESS,
                "device fault succeeded");
    TEST_ASSERT(pmap_extract(pmap, 0x1A000) == device_phys,
                "device mapping uses requested physical page");
    TEST_ASSERT(*(volatile uint8_t *)0x1A000 == 0xD2,
                "mapped device page is readable");

    current_process = saved_process;
    pmap_activate(pmap_kernel());
    vm_map_destroy(map);
    pmm_free_block(device_page);
    kprint("  PASS\n");
}

void test_vm_msync_dirty_writeback(void) {
    fs_node_t fake_file = {0};
    process_t fake_proc = {0};
    process_t *saved_process = current_process;
    pmap_t pmap;
    vm_map_t *map;
    vm_object_t *obj;
    vm_page_t *page;

    kprint("Test: vm_msync dirty writeback\n");

    memset(fake_store, 0, sizeof(fake_store));
    fake_pager_write_calls = 0;
    fake_file.impl = (uintptr_t)fake_store;
    fake_file.read = fake_pager_read;
    fake_file.write = fake_pager_write;

    pmap = pmap_create();
    TEST_ASSERT(pmap != 0, "pmap created");
    map = vm_map_create(pmap, 0x1000, 0x100000);
    TEST_ASSERT(map != NULL, "map created");

    fake_proc.vm_map = map;
    fake_proc.pmap = (struct pmap *)pmap;
    current_process = &fake_proc;

    obj = vm_object_allocate(VM_OBJ_TYPE_VNODE, 0x1000);
    TEST_ASSERT(obj != NULL, "object allocated");
    obj->pager = vm_pager_allocate(VM_OBJ_TYPE_VNODE, &fake_file, 0x1000, VM_PROT_READ | VM_PROT_WRITE, 0);
    TEST_ASSERT(obj->pager != NULL, "pager allocated");

    TEST_ASSERT(vm_map_insert(map, obj, 0, 0x18000, 0x19000,
                              VM_PROT_READ | VM_PROT_WRITE,
                              VM_PROT_READ | VM_PROT_WRITE,
                              VM_INHERIT_COPY) == 0,
                "map insert succeeded");

    page = vm_page_alloc(obj, 0, 0);
    TEST_ASSERT(page != NULL, "page allocated");
    vm_object_add_page(obj, page);

    memset((void *)(uintptr_t)(page->phys_addr + 0xC0000000), 0x6B, 4096);
    page->flags |= PG_VALID | PG_DIRTY;

    TEST_ASSERT(sys_msync((void *)0x18000, 4096, 0) == 0, "msync succeeded");
    TEST_ASSERT(fake_pager_write_calls > 0, "msync issued pager writeback");
    TEST_ASSERT(!(page->flags & PG_DIRTY), "page dirtiness cleared after writeback");
    TEST_ASSERT(fake_store[0] == 0x6B, "writeback persisted page contents");

    current_process = saved_process;
    pmap_activate(pmap_kernel());
    vm_map_destroy(map);
    kprint("  PASS\n");
}

void test_vm_mmap_file_private_cow(void) {
    fs_node_t fake_file = {0};
    file_t fake_fd = {0};
    process_t fake_proc = {0};
    process_t *saved_process = current_process;
    pmap_t pmap;
    vm_map_t *map;
    vm_map_entry_t *entry;
    vm_page_t *backing_page;
    vm_page_t *private_page;
    void *addr;

    kprint("Test: vm_mmap MAP_PRIVATE file COW\n");

    memset(fake_store, 0, sizeof(fake_store));
    fake_store[0] = 0x41;
    fake_store[255] = 0x5A;
    fake_pager_read_calls = 0;
    fake_pager_write_calls = 0;

    fake_file.flags = FS_FILE;
    fake_file.length = sizeof(fake_store);
    fake_file.impl = (uintptr_t)fake_store;
    fake_file.read = fake_pager_read;
    fake_file.write = fake_pager_write;

    fake_fd.f_data = &fake_file;
    fake_fd.f_flag = FREAD | FWRITE;

    pmap = pmap_create();
    TEST_ASSERT(pmap != 0, "pmap created");
    map = vm_map_create(pmap, 0x1000, 0x100000);
    TEST_ASSERT(map != NULL, "map created");

    fake_proc.vm_map = map;
    fake_proc.pmap = (struct pmap *)pmap;
    fake_proc.fds[3] = &fake_fd;
    current_process = &fake_proc;

    pmap_activate(pmap);

    addr = sys_mmap(NULL, 4096, VM_PROT_READ | VM_PROT_WRITE, MAP_PRIVATE, 3, 0);
    TEST_ASSERT(addr != (void *)-1, "private mmap succeeded");

    entry = vm_map_lookup(map, (uintptr_t)addr);
    TEST_ASSERT(entry != NULL, "entry created");
    TEST_ASSERT(entry->object != NULL, "entry has object");
    TEST_ASSERT(entry->object->shadow != NULL, "private mapping uses shadow object");
    TEST_ASSERT(entry->object->pager == NULL, "top-level object has no pager");
    TEST_ASSERT(entry->object->shadow->pager != NULL, "backing object has pager");

    TEST_ASSERT(vm_fault(map, (uintptr_t)addr, VM_PROT_READ) == VM_FAULT_SUCCESS, "private read fault succeeded");
    backing_page = vm_object_lookup_page(entry->object->shadow, 0);
    TEST_ASSERT(backing_page != NULL, "backing page populated");
    TEST_ASSERT(vm_object_lookup_page(entry->object, 0) == NULL, "top-level page not allocated on read");
    TEST_ASSERT(fake_pager_read_calls > 0, "pager read serviced private mapping");
    TEST_ASSERT(*(volatile uint8_t *)addr == 0x41, "private mapping read correct data");

    TEST_ASSERT(vm_fault(map, (uintptr_t)addr, VM_PROT_WRITE) == VM_FAULT_SUCCESS, "private write fault succeeded");
    private_page = vm_object_lookup_page(entry->object, 0);
    TEST_ASSERT(private_page != NULL, "private page allocated on write");
    TEST_ASSERT(private_page->phys_addr != backing_page->phys_addr, "private write copied backing page");

    *(volatile uint8_t *)addr = 0x77;
    private_page->flags |= PG_DIRTY;
    TEST_ASSERT(fake_store[0] == 0x41, "private write not reflected in backing file");
    TEST_ASSERT(sys_msync(addr, 4096, 0) == 0, "private msync returned success");
    TEST_ASSERT(fake_pager_write_calls == 0, "private msync did not write backing file");

    pmap_activate(pmap_kernel());
    current_process = saved_process;
    vm_map_destroy(map);
    kprint("  PASS\n");
}

void test_vm_mmap_file_shared_fork_visibility(void) {
    fs_node_t fake_file = {0};
    file_t fake_fd = {0};
    process_t parent_proc = {0};
    process_t child_proc = {0};
    process_t *saved_process = current_process;
    pmap_t parent_pmap;
    pmap_t child_pmap;
    vm_map_t *parent_map;
    vm_map_t *child_map;
    vm_map_entry_t *entry;
    void *addr;
    uintptr_t parent_pa;
    uintptr_t child_pa;

    kprint("Test: vm_mmap MAP_SHARED fork visibility\n");

    memset(fake_store, 0, sizeof(fake_store));
    fake_store[0] = 0x33;
    fake_pager_read_calls = 0;
    fake_pager_write_calls = 0;

    fake_file.flags = FS_FILE;
    fake_file.length = sizeof(fake_store);
    fake_file.impl = (uintptr_t)fake_store;
    fake_file.read = fake_pager_read;
    fake_file.write = fake_pager_write;

    fake_fd.f_data = &fake_file;
    fake_fd.f_flag = FREAD | FWRITE;

    parent_pmap = pmap_create();
    TEST_ASSERT(parent_pmap != 0, "parent pmap created");
    parent_map = vm_map_create(parent_pmap, 0x1000, 0x100000);
    TEST_ASSERT(parent_map != NULL, "parent map created");

    parent_proc.vm_map = parent_map;
    parent_proc.pmap = (struct pmap *)parent_pmap;
    parent_proc.fds[4] = &fake_fd;
    current_process = &parent_proc;

    pmap_activate(parent_pmap);
    addr = sys_mmap(NULL, 4096, VM_PROT_READ | VM_PROT_WRITE, MAP_SHARED, 4, 0);
    TEST_ASSERT(addr != (void *)-1, "shared mmap succeeded");

    entry = vm_map_lookup(parent_map, (uintptr_t)addr);
    TEST_ASSERT(entry != NULL, "shared entry created");
    TEST_ASSERT(entry->object != NULL, "shared object present");
    TEST_ASSERT(entry->object->pager != NULL, "shared mapping backed by vnode pager");
    TEST_ASSERT(entry->object->shadow == NULL, "shared mapping does not use shadow object");

    TEST_ASSERT(vm_fault(parent_map, (uintptr_t)addr, VM_PROT_WRITE) == VM_FAULT_SUCCESS, "parent shared fault succeeded");
    parent_pa = pmap_extract(parent_pmap, (uintptr_t)addr);
    TEST_ASSERT(parent_pa != 0, "parent physical mapping present");

    *(volatile uint8_t *)addr = 0x99;
    vm_object_lookup_page(entry->object, 0)->flags |= PG_DIRTY;

    child_pmap = pmap_create();
    TEST_ASSERT(child_pmap != 0, "child pmap created");
    child_map = vm_map_fork(parent_map, child_pmap);
    TEST_ASSERT(child_map != NULL, "child map forked");

    child_proc.vm_map = child_map;
    child_proc.pmap = (struct pmap *)child_pmap;
    current_process = &child_proc;
    pmap_activate(child_pmap);

    TEST_ASSERT(vm_fault(child_map, (uintptr_t)addr, VM_PROT_READ) == VM_FAULT_SUCCESS, "child shared fault succeeded");
    child_pa = pmap_extract(child_pmap, (uintptr_t)addr);
    TEST_ASSERT(child_pa == parent_pa, "shared mapping uses same physical page after fork");
    TEST_ASSERT(*(volatile uint8_t *)addr == 0x99, "child observes parent write");

    current_process = &parent_proc;
    pmap_activate(parent_pmap);
    TEST_ASSERT(sys_msync(addr, 4096, 0) == 0, "shared msync succeeded");
    TEST_ASSERT(fake_pager_write_calls > 0, "shared msync wrote backing file");
    TEST_ASSERT(fake_store[0] == 0x99, "shared write propagated to backing file");

    pmap_activate(pmap_kernel());
    current_process = saved_process;
    vm_map_destroy(child_map);
    vm_map_destroy(parent_map);
    kprint("  PASS\n");
}

void run_vm_pager_tests(void) {
    kprint("\n=== VM Pager Unit Tests ===\n");
    test_vm_pager_lifecycle();
    test_vm_pager_io();
    test_vm_swap_pager_roundtrip();
    test_vm_swap_pager_full();
    test_vm_device_fault_mapping();
    test_vm_msync_dirty_writeback();
    test_vm_mmap_file_private_cow();
    test_vm_mmap_file_shared_fork_visibility();
    kprint("\nVM Pager Tests Complete\n");
}
