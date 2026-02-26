#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

// Forward declarations
struct pmap;
typedef struct pmap *pmap_t;

// Mock headers or include real ones
// We will compile with -I../../sys/include -I../../include
#include <vm/vm_kmem.h>
#include <vm/vm_object.h>
#include <vm/vm_map.h>

// Mocks

// kmalloc/kfree
void *kmalloc(size_t size) {
    return calloc(1, size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

// vm_object mocks
// Note: vm_object struct is defined in vm_object.h, which we included.
// We implement the functions.

vm_object_t *vm_object_allocate(vm_object_type_t type, size_t size) {
    (void)type; (void)size;
    struct vm_object *obj = calloc(1, sizeof(struct vm_object));
    obj->ref_count = 1;
    // Initialize other fields if necessary
    return obj;
}

void vm_object_reference(struct vm_object *object) {
    if (object) {
        object->ref_count++;
    }
}

void vm_object_deallocate(struct vm_object *object) {
    if (object) {
        object->ref_count--;
        if (object->ref_count == 0) {
            free(object);
        }
    }
}

// pmap mocks
pmap_t pmap_create(void) { return (pmap_t)1; }
void pmap_destroy(pmap_t pmap) { (void)pmap; }
int pmap_protect(pmap_t pmap, uintptr_t start, uintptr_t end, uint32_t prot) {
    (void)pmap; (void)start; (void)end; (void)prot;
    return 0;
}

// Include the source file to test
#include "../../sys/vm/vm_map.c"

int main() {
    printf("Running vm_map_leak_test...\n");

    // 1. Create a map
    vm_map_t *map = vm_map_create((pmap_t)1, 0x1000, 0x100000);
    if (!map) {
        printf("Failed to create map\n");
        return 1;
    }

    // 2. Create an object
    struct vm_object *obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, 4096);
    if (!obj) {
        printf("Failed to allocate object\n");
        return 1;
    }
    // Initial ref count is 1

    // 3. Insert object into map
    // vm_map_insert takes ownership of the reference (conceptually)
    int ret = vm_map_insert(map, obj, 0, 0x10000, 0x11000, 7, 7, 1);
    if (ret != 0) {
        printf("Failed to insert\n");
        return 1;
    }

    // 4. Add an extra reference so we can verify it drops by one
    vm_object_reference(obj);
    // Now ref_count should be 2.
    int refs_before = obj->ref_count;
    printf("Ref count before remove: %d\n", refs_before);

    // 5. Remove the entry
    vm_map_remove(map, 0x10000, 0x11000);

    int refs_after = obj->ref_count;
    printf("Ref count after remove: %d\n", refs_after);

    bool passed = false;
    if (refs_after == refs_before - 1) {
        printf("PASS: Object dereferenced correctly.\n");
        passed = true;
    } else {
        printf("FAIL: Object leak or over-release! Expected %d, got %d.\n", refs_before - 1, refs_after);
    }

    // Cleanup
    vm_object_deallocate(obj); // Drop the extra reference we added
    vm_map_destroy(map);

    return passed ? 0 : 1;
}
