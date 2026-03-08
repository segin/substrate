/*
 * Unit tests for Pager subsystem
 */

#include <stdint.h>
#include <sys/types.h>
#include <vm/vm_pager.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_swap.h>
#include <vfs/vfs.h>
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

extern struct fs_node *swap_node;

static size_t fake_pager_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    uint8_t *store = (uint8_t *)(uintptr_t)node->impl;

    memcpy(buffer, store + offset, size);
    return size;
}

static size_t fake_pager_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    uint8_t *store = (uint8_t *)(uintptr_t)node->impl;

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

void run_vm_pager_tests(void) {
    kprint("\n=== VM Pager Unit Tests ===\n");
    test_vm_pager_lifecycle();
    test_vm_pager_io();
    test_vm_swap_pager_roundtrip();
    test_vm_swap_pager_full();
    kprint("\nVM Pager Tests Complete\n");
}
