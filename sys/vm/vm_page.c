#include "vm_page.h"
#include <stddef.h>
#include "../arch/i386/pmm.h" // For pmm_alloc_block

// Global page queues
static vm_page_t *free_queue = NULL;
static vm_page_t *active_queue = NULL;
static vm_page_t *inactive_queue = NULL;

void vm_page_init(void) {
    // Initialize queues
    free_queue = NULL;
    active_queue = NULL;
    inactive_queue = NULL;
}

// Internal helper to add a page to the head of a queue
static void enqueue(vm_page_t **head, vm_page_t *page) {
    page->next = *head;
    page->prev = NULL;
    if (*head) {
        (*head)->prev = page;
    }
    *head = page;
}

// Internal helper to remove a page from a queue
static void dequeue(vm_page_t **head, vm_page_t *page) {
    if (page->prev) {
        page->prev->next = page->next;
    } else {
        *head = page->next;
    }
    if (page->next) {
        page->next->prev = page->prev;
    }
    page->next = NULL;
    page->prev = NULL;
}

vm_page_t *vm_page_alloc(struct vm_object *object, uint64_t pindex, int req) {
    (void)req; // Ignore flags for now
    
    vm_page_t *page = NULL;

    // 1. Try to get a page from the free queue
    if (free_queue) {
        page = free_queue;
        dequeue(&free_queue, page);
        page->flags &= ~PG_FREE;
    } else {
        // 2. If free queue is empty, allocate from PMM
        void *phys = pmm_alloc_block();
        if (!phys) return NULL; // OOM

        // We need a kernel heap to allocate the vm_page_t structure itself!
        // But we haven't implemented kmalloc yet. 
        // Chicken and egg problem.
        
        // For this stage, we will cheat and assume we can just cast the physical 
        // address to a vm_page_t wrapper if we had a 1:1 array of struct pages 
        // (like Linux mem_map).
        
        // Since we don't have that array yet, we'll return NULL for now 
        // until 'kmalloc' is implemented, effectively disabling dynamic growth.
        // OR we can use a static pool for bootstrap.
        
        return NULL; 
    }

    // Initialize page
    page->object = object;
    page->pindex = pindex;
    page->ref_count = 1;
    page->flags |= PG_BUSY;

    // Add to active queue by default? Or let caller decide?
    // Usually caller puts it in active or inactive.
    
    return page;
}

void vm_page_free(vm_page_t *m) {
    if (!m) return;
    
    // Sanity check: detect obviously corrupted pointers
    // Valid kernel pointers should be in higher half (>= 0xC0000000)
    if ((uintptr_t)m < 0xC0000000 || (uintptr_t)m >= 0xF0000000) {
        // Corrupted pointer - log and return to avoid crash
        extern void kprint(const char *);
        kprint("vm_page_free: corrupted page pointer detected!\n");
        return;
    }

    // Remove from whatever queue it's in
    if (m->flags & PG_ACTIVE) {
        dequeue(&active_queue, m);
        m->flags &= ~PG_ACTIVE;
    } else if (m->flags & PG_INACTIVE) {
        dequeue(&inactive_queue, m);
        m->flags &= ~PG_INACTIVE;
    }

    // Reset state
    m->object = NULL;
    m->pindex = 0;
    m->ref_count = 0;
    
    // Add to free queue
    enqueue(&free_queue, m);
    m->flags |= PG_FREE;
}
