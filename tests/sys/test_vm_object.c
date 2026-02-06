/*
 * Unit tests for vm_object subsystem
 */

#include <stdint.h>
#include <vm/vm_object.h>
#include <vm/vm_kmem.h>
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

// Test 1: Allocate and deallocate
void test_vm_object_lifecycle(void) {
    kprint("Test: vm_object lifecycle\n");
    
    vm_object_t *obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, 0x10000);
    TEST_ASSERT(obj != NULL, "alloc returned obj");
    TEST_ASSERT(obj->ref_count == 1, "ref_count is 1");
    TEST_ASSERT(obj->size == 0x10000, "size correct");
    TEST_ASSERT(obj->type == VM_OBJ_TYPE_DEFAULT, "type correct");
    
    vm_object_reference(obj);
    TEST_ASSERT(obj->ref_count == 2, "ref_count incremented");
    
    vm_object_deallocate(obj);
    TEST_ASSERT(obj->ref_count == 1, "ref_count decremented");
    
    vm_object_deallocate(obj);
    // Object should be freed (or marked dead if checking internals)
    kprint("  PASS\n");
}

// Test 2: Shadow objects
void test_vm_object_shadow(void) {
    kprint("Test: vm_object shadow\n");
    
    vm_object_t *src = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, 0x1000);
    vm_object_t *shadow = vm_object_shadow(src);
    
    TEST_ASSERT(shadow != NULL, "shadow created");
    TEST_ASSERT(shadow->shadow == src, "shadow points to src");
    TEST_ASSERT(src->ref_count == 2, "src ref_count incremented");
    TEST_ASSERT(src->flags & VM_OBJ_COPY, "src marked COPY");
    
    vm_object_deallocate(shadow);
    TEST_ASSERT(src->ref_count == 1, "src ref_count decremented after shadow free");
    
    vm_object_deallocate(src);
    kprint("  PASS\n");
}

// Test 3: Page management
void test_vm_object_pages(void) {
    kprint("Test: vm_object page list\n");
    
    vm_object_t *obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, 0x1000);
    
    // Fake page for testing
    vm_page_t page = {0};
    page.pindex = 5;
    
    vm_object_add_page(obj, &page);
    TEST_ASSERT(obj->page_count == 1, "page count 1");
    TEST_ASSERT(page.object == obj, "page points to obj");
    
    vm_page_t *p = vm_object_lookup_page(obj, 5);
    TEST_ASSERT(p == &page, "lookup found page");
    
    p = vm_object_lookup_page(obj, 99);
    TEST_ASSERT(p == NULL, "lookup missing page returns NULL");
    
    vm_object_remove_page(obj, &page);
    TEST_ASSERT(obj->page_count == 0, "page count 0");
    TEST_ASSERT(page.object == NULL, "page object cleared");
    
    vm_object_deallocate(obj);
    kprint("  PASS\n");
}

// Test 4: Dynamic allocation and freeing
void test_vm_object_dynamic_free(void) {
    kprint("Test: vm_object dynamic free\n");

    // Allocate many objects to ensure we exhaust bootstrap pool
    // MAX_BOOTSTRAP_OBJECTS is 32 in vm_object.c
    const int count = 50;
    vm_object_t *objects[50];

    for (int i = 0; i < count; i++) {
        objects[i] = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, 0x1000);
        TEST_ASSERT(objects[i] != NULL, "allocation failed");
    }

    // Get current frees count
    uint64_t frees_before = 0;
    kmem_get_stats(NULL, &frees_before, NULL);

    // Deallocate the last object (definitely dynamic)
    vm_object_deallocate(objects[count - 1]);
    objects[count - 1] = NULL;

    // Get new frees count
    uint64_t frees_after = 0;
    kmem_get_stats(NULL, &frees_after, NULL);

    TEST_ASSERT(frees_after > frees_before, "kfree called for dynamic object");

    // Clean up remaining
    for (int i = 0; i < count - 1; i++) {
        vm_object_deallocate(objects[i]);
    }

    kprint("  PASS\n");
}

void run_vm_object_tests(void) {
    kprint("\n=== VM Object Unit Tests ===\n");
    test_vm_object_lifecycle();
    test_vm_object_shadow();
    test_vm_object_pages();
    test_vm_object_dynamic_free();
    kprint("\nVM Object Tests Complete\n");
}
