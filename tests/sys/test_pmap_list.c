#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// Override KERNEL_BASE so pointers work on host
#define KERNEL_BASE 0

// Mock headers guards to skip them
#define _PMM_H
#define _VM_KMEM_H

// Mock types/funcs from pmm.h
void* pmm_alloc_block(void);
void pmm_free_block(void* p);

// Mock types/funcs from vm_kmem.h
void *kmalloc(size_t size);
void kfree(void *ptr, size_t size);

// Mock boot_pml4
uint64_t boot_pml4[512] __attribute__((aligned(4096)));

// We need to define spinlock types/funcs because we don't link kernel sources
// We can't rely on <sys/lock.h> providing implementation, only types.
// But we can include the header for types.
// We'll define the functions here.

// Mock spinlock impl
#include <sys/lock.h>

void spinlock_init(spinlock_t *lock, const char *name) {
    lock->locked = 0;
    lock->cpu_id = 0;
    lock->name = name;
}
void spinlock_acquire(spinlock_t *lock) {
    assert(lock->locked == 0); // Simple non-recursive check
    lock->locked = 1;
}
void spinlock_release(spinlock_t *lock) {
    assert(lock->locked == 1);
    lock->locked = 0;
}

// Implement mocks
void* pmm_alloc_block(void) {
    return calloc(1, 4096);
}
void pmm_free_block(void* p) {
    free(p);
}
void *kmalloc(size_t size) {
    return calloc(1, size);
}
void kfree(void *ptr, size_t size) {
    free(ptr);
}

// Mock LAPIC functions (used by pmap.c)
void lapic_send_eoi(void) {}
void lapic_send_ipi_all_excl_self(int vector) {}

// Include the source file directly
// We use relative path from tests/sys/
#include "../../sys/arch/x86_64/pmap.c"

int main() {
    printf("Testing pmap list tracking...\n");

    // Test 1: Init
    printf("Calling pmap_init...\n");
    pmap_init();
    printf("pmap_init done.\n");

    // Verify kernel pmap is in the list
    if (TAILQ_EMPTY(&global_pmap_list)) {
        printf("FAILED: Global pmap list is empty after init\n");
        return 1;
    }
    struct pmap *kpmap = TAILQ_FIRST(&global_pmap_list);
    if (kpmap != &kernel_pmap_store) {
        printf("FAILED: First pmap is not kernel pmap\n");
        return 1;
    }
    printf("Init passed.\n");

    // Test 2: Create
    printf("Creating p1...\n");
    pmap_t p1 = pmap_create();
    printf("p1 created.\n");
    if (p1 == NULL) {
        printf("FAILED: pmap_create returned NULL\n");
        return 1;
    }

    // Verify p1 is in list (tail)
    struct pmap *last = TAILQ_LAST(&global_pmap_list, pmap_list);
    if (last != p1) {
        printf("FAILED: pmap_create did not add to tail\n");
        return 1;
    }
    printf("Create passed.\n");

    // Test 3: Create another
    printf("Creating p2...\n");
    pmap_t p2 = pmap_create();
    printf("p2 created.\n");
    if (p2 == NULL) {
        printf("FAILED: pmap_create p2 returned NULL\n");
        return 1;
    }

    last = TAILQ_LAST(&global_pmap_list, pmap_list);
    if (last != p2) {
        printf("FAILED: pmap_create p2 did not add to tail\n");
        return 1;
    }

    // Verify order: Kernel -> p1 -> p2
    struct pmap *iter = TAILQ_FIRST(&global_pmap_list);
    assert(iter == kpmap);
    iter = TAILQ_NEXT(iter, list_entry);
    assert(iter == p1);
    iter = TAILQ_NEXT(iter, list_entry);
    assert(iter == p2);
    iter = TAILQ_NEXT(iter, list_entry);
    assert(iter == NULL);
    printf("Ordering passed.\n");

    // Test 4: Destroy p1
    printf("Destroying p1...\n");
    pmap_destroy(p1);
    printf("p1 destroyed.\n");

    // Verify list: Kernel -> p2
    iter = TAILQ_FIRST(&global_pmap_list);
    assert(iter == kpmap);
    iter = TAILQ_NEXT(iter, list_entry);
    assert(iter == p2);
    iter = TAILQ_NEXT(iter, list_entry);
    assert(iter == NULL);
    printf("Destroy p1 passed.\n");

    // Test 5: Destroy p2
    printf("Destroying p2...\n");
    pmap_destroy(p2);
    printf("p2 destroyed.\n");

    // Verify list: Kernel only
    iter = TAILQ_FIRST(&global_pmap_list);
    assert(iter == kpmap);
    iter = TAILQ_NEXT(iter, list_entry);
    assert(iter == NULL);
    printf("Destroy p2 passed.\n");

    printf("All tests passed!\n");
    return 0;
}
