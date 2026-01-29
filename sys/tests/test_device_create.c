/*
 * test_device_create.c
 *
 * Unit Tests for device_create allocator.
 */

#include <kern/device.h>
#include <stddef.h>
#include <string.h>

/* Mock kmalloc/kfree for unit testing if not linked against full kernel */
/* But in the test framework we link against kern.o or lib.o? 
   The makefile links all .o files. We need to make sure we don't double define if simple mock is needed.
   Actually, the test environment usually has a test runner. 
   We will rely on the real kmalloc if linked, but here we can't ensure it's functional without PMM. 
   Wait, tests run in kernel environment? 
   "Building Kernel Tests... cc ... -ffreestanding"
   So we are in the kernel. `kmalloc` should work if PMM is init.
   If this test runs early, it might fail.
   However, test_device_struct was simple struct.
   For this, let's assume we can mock it or use a simple static buffer if we want to be safe,
   or just assume the test runner initializes heap.
   Given TASKS.md says "Tests: unit", I'll trust the environment or use a mock allocator for this specific test file if safe.
   Let's use a mock allocator to strictly test logic without PMM dependencies.
*/

static char test_heap[4096];
static size_t heap_idx = 0;

void *mock_kmalloc(size_t size) {
    if (heap_idx + size > sizeof(test_heap)) return NULL;
    void *ptr = &test_heap[heap_idx];
    heap_idx += size;
    return ptr;
}

void mock_kfree(void *ptr) {
    (void)ptr;
}

/* Redefine kmalloc/kfree to mocks for this translation unit? 
   No, device.c is compiled separately.
   We need to link device.o. 
   If device.c calls kmalloc, it calls the global kmalloc.
   So we can't easily mock it unless we verify integration.
   
   If we are testing `device_create` which is in `sys/kern/device.c`,
   we must include `sys/kern/device.o` in the link.
   `sys/kern/device.o` depends on `kmalloc`.
   
   If we run this test, we need `kmalloc`.
   Let's check if `test_uma.c` usage.
*/

struct device *device_create(const char *name, struct device *parent);

int test_device_allocation(void) {
    struct device *root = device_create("root", NULL);
    if (!root) return -1;
    if (root->ref_count != 1) return -2;
    if (strcmp(root->name, "root") != 0) return -3;
    if (root->parent != NULL) return -4;

    struct device *child = device_create("child", root);
    if (!child) return -5;
    if (child->parent != root) return -6;
    
    /* Verify linking */
    if (root->children != child) return -7;
    
    /* Add second child */
    struct device *child2 = device_create("child2", root);
    if (root->children != child2) return -8; /* Head insertion */
    if (child2->sibling != child) return -9;
    
    return 0;
}
