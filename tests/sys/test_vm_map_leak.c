#define HOST_TEST

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Mock kmem counters
static int alloc_count = 0;
static int free_count = 0;

// Mock kmem functions
// These must match signatures in vm/vm_kmem.h (which vm_map.c includes)
void *kmalloc(size_t size) {
    alloc_count++;
    return malloc(size);
}

void kfree(void *ptr, size_t size) {
    if (ptr) {
        free_count++;
        free(ptr);
    }
}

void *kzalloc(size_t size) {
    void *p = kmalloc(size);
    if(p) memset(p, 0, size);
    return p;
}

// Include headers from the kernel to get types and declarations
// Use relative paths or assume include paths are set in compiler command
#include <vm/vm_object.h>
#include <vm/vm_map.h>
#include <arch/i386/pmap.h>

// Implement vm_object mocks matching vm_object.h
void vm_object_deallocate(vm_object_t *obj) {
    if (obj) {
        obj->ref_count--;
        if (obj->ref_count == 0) {
            kfree(obj, sizeof(vm_object_t));
        }
    }
}

void vm_object_reference(vm_object_t *obj) {
    if (obj) {
        obj->ref_count++;
    }
}

// Implement pmap mocks matching arch/i386/pmap.h
int pmap_protect(pmap_t pmap, uintptr_t start, uintptr_t end, uint32_t prot) {
    (void)pmap; (void)start; (void)end; (void)prot;
    return 0;
}
void pmap_destroy(pmap_t pmap) {
    (void)pmap;
}

// Mock vm_object_allocate for test usage (since we don't link vm_object.c)
vm_object_t *test_alloc_object(void) {
    vm_object_t *obj = kmalloc(sizeof(vm_object_t));
    if (obj) {
        memset(obj, 0, sizeof(vm_object_t));
        obj->ref_count = 1;
    }
    return obj;
}

// Include the source file under test
#include "../sys/vm/vm_map.c"

int main() {
    printf("Starting reproduction test...\n");

    // Reset counters
    alloc_count = 0;
    free_count = 0;

    // Create a map
    // vm_map_create calls kmalloc for map and alloc_entry for sentinel
    vm_map_t *map = vm_map_create(NULL, 0, 0xFFFFFFFF);
    if (!map) {
        printf("Failed to create map\n");
        return 1;
    }

    int initial_allocs = alloc_count;
    int initial_frees = free_count;
    printf("Map created. Allocs: %d, Frees: %d\n", initial_allocs, initial_frees);

    // Create an object
    vm_object_t *obj = test_alloc_object();
    printf("Object created. RefCount: %d\n", obj->ref_count);

    // Insert object into map
    // This should allocate one entry.
    if (vm_map_insert(map, obj, 0, 0x1000, 0x2000, 0, 0, 0) != 0) {
        printf("Failed to insert entry\n");
        return 1;
    }
    printf("Entry inserted. Allocs: %d\n", alloc_count);

    // Now remove the entry
    // This should free the entry AND deallocate the object (decrement ref, then free object)
    vm_map_remove(map, 0x1000, 0x2000);

    printf("Entry removed. Allocs: %d, Frees: %d\n", alloc_count, free_count);

    // Expected frees: initial_frees + 2 (entry + object)
    // entry is freed by free_entry (calls kfree)
    // object is freed by vm_object_deallocate (calls kfree)

    int expected_frees = initial_frees + 2;
    if (free_count == expected_frees) {
        printf("SUCCESS: Object and Entry freed correctly.\n");
    } else {
        printf("FAILURE: Expected %d frees, got %d. Memory Leak detected!\n", expected_frees, free_count);
        vm_map_destroy(map);
        return 1;
    }

    vm_map_destroy(map);
    return 0;
}
