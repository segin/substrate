#include "vm_page.h"
#include <stddef.h>
#include "../arch/i386/pmm.h" // For pmm_alloc_block

// Global page queues
static vm_page_t *free_queue = NULL;
static vm_page_t *active_queue = NULL;
static vm_page_t *inactive_queue = NULL;
static vm_page_t *wired_queue = NULL;      // Pages pinned in memory (kernel, DMA)
static vm_page_t *laundry_queue = NULL;    // Dirty pages pending writeback

void vm_page_init(void) {
    // Initialize queues
    free_queue = NULL;
    active_queue = NULL;
    inactive_queue = NULL;
    wired_queue = NULL;
    laundry_queue = NULL;
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

        // Use the globally allocated page array in PMM
        page = pmm_get_page((uintptr_t)phys);
        if (!page) return NULL;
        
        page->flags &= ~PG_FREE;
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

// Move page to active queue (recently accessed)
void vm_page_activate(vm_page_t *m) {
    if (!m || (m->flags & PG_ACTIVE)) return;
    
    // Remove from current queue
    if (m->flags & PG_INACTIVE) {
        dequeue(&inactive_queue, m);
        m->flags &= ~PG_INACTIVE;
    } else if (m->flags & PG_FREE) {
        dequeue(&free_queue, m);
        m->flags &= ~PG_FREE;
    }
    
    enqueue(&active_queue, m);
    m->flags |= PG_ACTIVE;
}

// Move page to inactive queue (eviction candidate)
void vm_page_deactivate(vm_page_t *m) {
    if (!m || (m->flags & PG_INACTIVE)) return;
    
    // Remove from active queue
    if (m->flags & PG_ACTIVE) {
        dequeue(&active_queue, m);
        m->flags &= ~PG_ACTIVE;
    }
    
    enqueue(&inactive_queue, m);
    m->flags |= PG_INACTIVE;
}

// Wire page (pin in memory, cannot be paged out)
void vm_page_wire(vm_page_t *m) {
    if (!m) return;
    
    m->wire_count++;
    
    // Remove from LRU queues if being wired for first time
    if (m->wire_count == 1) {
        if (m->flags & PG_ACTIVE) {
            dequeue(&active_queue, m);
            m->flags &= ~PG_ACTIVE;
        } else if (m->flags & PG_INACTIVE) {
            dequeue(&inactive_queue, m);
            m->flags &= ~PG_INACTIVE;
        }
        enqueue(&wired_queue, m);
    }
}

// Unwire page (allow paging out when wire_count reaches 0)
void vm_page_unwire(vm_page_t *m) {
    if (!m || m->wire_count == 0) return;
    
    m->wire_count--;
    
    // Move back to active queue when fully unwired
    if (m->wire_count == 0) {
        dequeue(&wired_queue, m);
        enqueue(&active_queue, m);
        m->flags |= PG_ACTIVE;
    }
}
