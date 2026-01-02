#include "vm_page.h"
#include <stddef.h>

// Global page queues
static vm_page_t *free_queue = NULL;
static vm_page_t *active_queue = NULL;
static vm_page_t *inactive_queue = NULL;

void vm_page_init(void) {
    // Initialize queues
    free_queue = NULL;
    active_queue = NULL;
    inactive_queue = NULL;
    
    // In a real implementation, we would take the free pages from PMM
    // and create vm_page_t structures for them here.
}

vm_page_t *vm_page_alloc(struct vm_object *object, uint64_t pindex, int req) {
    (void)object; (void)pindex; (void)req;
    // Stub: pop from free_queue
    return NULL;
}

void vm_page_free(vm_page_t *m) {
    (void)m;
    // Stub: push to free_queue
}
