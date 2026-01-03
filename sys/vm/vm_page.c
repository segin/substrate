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

// External pmap functions (to be implemented in pmap layer)
extern int pmap_is_referenced(vm_page_t *m);
extern void pmap_clear_reference(vm_page_t *m);

// LRU scanner: Walk active queue and move unreferenced pages to inactive
// Returns number of pages deactivated
int vm_pageout_scan(int max_scan) {
    int scanned = 0;
    int deactivated = 0;
    
    vm_page_t *m = active_queue;
    
    while (m && scanned < max_scan) {
        vm_page_t *next = m->next;  // Save next before potential dequeue
        scanned++;
        
        // Skip wired pages (shouldn't be on active queue but check anyway)
        if (m->wire_count > 0) {
            m = next;
            continue;
        }
        
        // Check if page was recently accessed via PTE A bit
        if (pmap_is_referenced(m)) {
            // Page was accessed - give it second chance
            pmap_clear_reference(m);
            // Move to head of active queue (most recently used)
            dequeue(&active_queue, m);
            enqueue(&active_queue, m);
            m->flags |= PG_ACTIVE;  // Keep active flag
        } else {
            // Page not accessed - move to inactive queue
            dequeue(&active_queue, m);
            m->flags &= ~PG_ACTIVE;
            enqueue(&inactive_queue, m);
            m->flags |= PG_INACTIVE;
            deactivated++;
        }
        
        m = next;
    }
    
    return deactivated;
}

// ==================== Page Daemon ====================
// Background daemon that frees pages when memory is low

// Thresholds (pages)
static int vm_page_free_min = 16;      // Panic below this
static int vm_page_free_target = 64;   // Daemon sleeps above this
static int __attribute__((unused)) vm_page_free_reserved = 8;  // Reserved for kernel emergencies

// Statistics (some not yet used - will be exposed via /proc/vmstat)
static int vm_stat_free_count = 0;
static int __attribute__((unused)) vm_stat_active_count = 0;
static int vm_stat_inactive_count = 0;
static int __attribute__((unused)) vm_stat_wire_count = 0;
static int vm_stat_pageouts = 0;
static int __attribute__((unused)) vm_stat_pageins = 0;
static int __attribute__((unused)) vm_stat_faults = 0;

// Wakeup flag for daemon
static volatile int vm_pages_needed = 0;

// Try to free a clean inactive page
// Returns 1 if page was freed, 0 otherwise
int vm_page_try_to_free(vm_page_t *m) {
    if (!m) return 0;
    
    // Cannot free dirty pages directly
    if (m->flags & PG_DIRTY) return 0;
    
    // Cannot free busy pages
    if (m->flags & PG_BUSY) return 0;
    
    // Cannot free wired pages
    if (m->wire_count > 0) return 0;
    
    // Free the page back to PMM
    vm_page_free(m);
    vm_stat_free_count++;
    return 1;
}

// Move dirty page to laundry queue for writeback
void vm_page_launder(vm_page_t *m) {
    if (!m || !(m->flags & PG_DIRTY)) return;
    
    // Remove from inactive queue
    if (m->flags & PG_INACTIVE) {
        dequeue(&inactive_queue, m);
        m->flags &= ~PG_INACTIVE;
        vm_stat_inactive_count--;
    }
    
    // Add to laundry queue
    enqueue(&laundry_queue, m);
    // Note: Actual writeout would happen here via vm_object's backing store
    // For now, just mark as clean after "writing"
    m->flags &= ~PG_DIRTY;
    vm_stat_pageouts++;
    
    // Move back to inactive after "cleaning"
    dequeue(&laundry_queue, m);
    enqueue(&inactive_queue, m);
    m->flags |= PG_INACTIVE;
    vm_stat_inactive_count++;
}

// Main pageout daemon loop
// Called periodically or when vm_pages_needed is set
void vm_pageout(void) {
    extern void kprint(const char *);
    
    // Check if we need to free pages
    if (vm_stat_free_count >= vm_page_free_target && !vm_pages_needed) {
        return;  // Plenty of free memory
    }
    
    int freed = 0;
    int target = vm_page_free_target - vm_stat_free_count;
    if (target < 0) target = 0;
    
    // Phase 1: Scan active queue, move cold pages to inactive
    vm_pageout_scan(target * 2);
    
    // Phase 2: Try to free clean inactive pages
    vm_page_t *m = inactive_queue;
    while (m && freed < target) {
        vm_page_t *next = m->next;
        
        if (!(m->flags & PG_DIRTY) && !(m->flags & PG_BUSY) && m->wire_count == 0) {
            dequeue(&inactive_queue, m);
            m->flags &= ~PG_INACTIVE;
            vm_stat_inactive_count--;
            
            if (vm_page_try_to_free(m)) {
                freed++;
            }
        }
        
        m = next;
    }
    
    // Phase 3: Launder dirty pages if still short
    if (freed < target) {
        m = inactive_queue;
        while (m && freed < target) {
            vm_page_t *next = m->next;
            
            if ((m->flags & PG_DIRTY) && !(m->flags & PG_BUSY)) {
                vm_page_launder(m);
                // Try to free now that it's clean
                if (vm_page_try_to_free(m)) {
                    freed++;
                }
            }
            
            m = next;
        }
    }
    
    // Clear request
    vm_pages_needed = 0;
    
    // Panic if critically low
    if (vm_stat_free_count < vm_page_free_min) {
        kprint("PANIC: Out of memory!\n");
        // OOM killer would go here
    }
}

// Signal that pages are needed
void vm_page_wakeup_daemon(void) {
    vm_pages_needed = 1;
}
