#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

// Mock kmem
void *kmalloc(size_t size) { return malloc(size); }
void kfree(void *ptr, size_t size) { (void)size; free(ptr); }
void *kzalloc(size_t size) { return calloc(1, size); }

// Includes
// We include headers first to get types
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <arch/i386/pmap.h>

// Mock vm_object
void vm_object_reference(struct vm_object *obj) {
    if (obj) obj->ref_count++;
}
void vm_object_deallocate(struct vm_object *obj) {
    if (obj) {
        // Just mock
    }
}
// Signature match
struct vm_object *vm_object_allocate(vm_object_type_t type, size_t size) {
    (void)type; (void)size;
    return NULL;
}

// Mock pmap
pmap_t pmap_create(void) { return (pmap_t)1; }
void pmap_destroy(pmap_t pmap) { (void)pmap; }
// Signature match
int pmap_protect(pmap_t pmap, uintptr_t s, uintptr_t e, uint32_t p) {
    (void)pmap; (void)s; (void)e; (void)p;
    return 0;
}
void pmap_reference(pmap_t pmap) { (void)pmap; }

// Include source
#include "../../sys/vm/vm_map.c"

int main(void) {
    // Benchmark vm_map_find_space
    pmap_t pmap = pmap_create();
    vm_map_t *map = vm_map_create(pmap, 0x1000, 0x40000000);
    if (!map) {
        printf("Failed to create map\n");
        return 1;
    }

    size_t alloc_size = 0x1000;
    int iterations = 100000;

    // Fill first 10000 slots
    int fill_count = 10000;
    uintptr_t current = 0x1000;
    for (int i = 0; i < fill_count; i++) {
        vm_map_insert(map, NULL, 0, current, current + 0x1000, 7, 7, 0);
        current += 0x1000;
    }

    // Reset hint to beginning to force full scan
    // vm_map_lookup updates hint
    vm_map_lookup(map, 0x1000);

    printf("Map filled with %d entries. Starting search benchmark (%d iterations)...\n", fill_count, iterations);

    struct timespec ts_start, ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    for (int i = 0; i < iterations; i++) {
        uintptr_t addr;
        if (vm_map_find_space(map, &addr, alloc_size) != 0) {
             printf("Error finding space\n");
             break;
        }
        if (addr != current) {
            printf("Unexpected address: %lx (expected %lx)\n", (unsigned long)addr, (unsigned long)current);
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &ts_end);

    double duration = (ts_end.tv_sec - ts_start.tv_sec) + (ts_end.tv_nsec - ts_start.tv_nsec) / 1e9;
    printf("Duration: %.6f seconds\n", duration);
    printf("Average per op: %.6f us\n", (duration * 1e6) / iterations);

    vm_map_destroy(map);
    return 0;
}
