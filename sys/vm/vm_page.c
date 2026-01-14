#include "vm_page.h"
#include "vm_pager.h"
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

// Hold page (increment ref_count for mapping)
void vm_page_hold(vm_page_t *m) {
    if (!m) return;
    m->ref_count++;
}

// Unhold page (decrement ref_count, free if zero and not wired)
void vm_page_unhold(vm_page_t *m) {
    if (!m || m->ref_count == 0) return;
    m->ref_count--;
    // Note: Page is freed by vm_page_free() when no longer needed
    // ref_count reaching 0 indicates no active mappings, but page may
    // still be cached in inactive queue for potential reuse
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
    
    // Perform writeback via pager
    bool success = false;
    if (m->object && m->object->pager) {
        // Asynchronous or synchronous?
        // Page daemon should probably block or queue async IO. 
        // For simplicity, we do synchronous write here (might block daemon).
        if (vm_pager_put_pages(m->object->pager, &m, 1, true) == 0) {
            success = true;
        }
    } else if (m->object && m->object->type == VM_OBJ_TYPE_DEFAULT) {
        // Anonymous memory without pager yet?
        // Should have been assigned a swap pager.
        // If not, we can't page it out.
    }
    
    // Remove from laundry queue
    dequeue(&laundry_queue, m);

    if (success) {
        // Mark clean
        m->flags &= ~PG_DIRTY;
        vm_stat_pageouts++;
        
        // Move to inactive queue (now clean, so next scan can free it)
        enqueue(&inactive_queue, m);
        m->flags |= PG_INACTIVE;
        vm_stat_inactive_count++;
    } else {
        // Failed to swap out (no swap space?)
        // Reactivate it to avoid tight loop of failing laundry
        enqueue(&active_queue, m);
        m->flags |= PG_ACTIVE;
    }
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
    
    // Phase 0: Reclaim kernel memory (UMA, etc.)
    extern void uma_reclaim(void);
    uma_reclaim();
    
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

// ==================== Writeback Tracking ====================
// Track pages that need writeback to swap/file

// Check if page needs writeback to backing store
int vm_page_needs_writeback(vm_page_t *m) {
    if (!m) return 0;
    
    // Page needs writeback if:
    // - It's marked dirty (PG_DIRTY) and not already being written (PG_WRITEBACK)
    // - OR it's explicitly marked as needing sync (PG_NEEDSYNC)
    if ((m->flags & PG_DIRTY) && !(m->flags & PG_WRITEBACK)) {
        return 1;
    }
    if (m->flags & PG_NEEDSYNC) {
        return 1;
    }
    return 0;
}

// Mark page for writeback (sets PG_WRITEBACK, starts I/O)
void vm_page_mark_for_writeback(vm_page_t *m) {
    if (!m) return;
    
    // Don't start writeback on already writing page
    if (m->flags & PG_WRITEBACK) return;
    
    // Set writeback in progress
    m->flags |= PG_WRITEBACK;
    m->flags |= PG_BUSY;  // Page is busy during I/O
    
    // Record time for scheduling (use uptime as monotonic timestamp)
    extern int64_t get_uptime(void);
    m->last_modified = (uint32_t)get_uptime();
}

// Complete writeback (called when I/O finishes)
void vm_page_writeback_done(vm_page_t *m) {
    if (!m) return;
    
    // Clear writeback flags
    m->flags &= ~PG_WRITEBACK;
    m->flags &= ~PG_BUSY;
    m->flags &= ~PG_NEEDSYNC;
    
    // If no one dirtied the page during writeback, it's now clean
    // (If someone did, PG_DIRTY will still be set from the new write)
    if (!(m->flags & PG_DIRTY)) {
        // Page is clean - can be freed without writeback
    }
}

// ==================== Page Aging Algorithm ====================
// Implements clock/LRU approximation for page replacement

// External pmap function for access tracking
extern void pmap_track_access(vm_page_t *m);

// Periodic scan of all resident pages - decrement age if not accessed
// Called by page daemon periodically
void vm_page_age_scan(void) {
    extern int64_t get_uptime(void);
    (void)get_uptime;  // Suppress unused warning if not needed
    
    // Scan active queue - pages accessed get max age, others decrement
    vm_page_t *m = active_queue;
    while (m) {
        vm_page_t *next = m->next;
        
        // Skip wired pages
        if (m->wire_count > 0) {
            m = next;
            continue;
        }
        
        // Check A-bit via pmap and update access tracking
        if (pmap_is_referenced(m)) {
            // Page was accessed - reset age to max
            m->age = VM_PAGE_AGE_MAX;
            pmap_clear_reference(m);
        } else {
            // Page not accessed - decrement age
            if (m->age > 0) {
                m->age--;
            }
            // If age reaches 0, move to inactive queue
            if (m->age == 0) {
                dequeue(&active_queue, m);
                m->flags &= ~PG_ACTIVE;
                enqueue(&inactive_queue, m);
                m->flags |= PG_INACTIVE;
            }
        }
        
        m = next;
    }
    
    // Scan inactive queue - further age inactive pages
    m = inactive_queue;
    while (m) {
        vm_page_t *next = m->next;
        
        // Skip wired pages
        if (m->wire_count > 0) {
            m = next;
            continue;
        }
        
        // Check if page was accessed while inactive
        if (pmap_is_referenced(m)) {
            // Reactivate
            dequeue(&inactive_queue, m);
            m->flags &= ~PG_INACTIVE;
            enqueue(&active_queue, m);
            m->flags |= PG_ACTIVE;
            m->age = VM_PAGE_AGE_INITIAL;  // Give a second chance
            pmap_clear_reference(m);
        }
        
        m = next;
    }
}

// Check if page is a candidate for eviction (age=0, not wired, not busy)
int vm_page_is_evict_candidate(vm_page_t *m) {
    if (!m) return 0;
    
    // Wired pages cannot be evicted
    if (m->wire_count > 0) return 0;
    
    // Busy pages cannot be evicted
    if (m->flags & PG_BUSY) return 0;
    
    // Pages in writeback cannot be evicted
    if (m->flags & PG_WRITEBACK) return 0;
    
    // Active pages are not candidates (need to age out first)
    if (m->flags & PG_ACTIVE) return 0;
    
    // Inactive pages with age 0 are primary candidates
    if ((m->flags & PG_INACTIVE) && m->age == 0) {
        return 1;
    }
    
    return 0;
}

// ==================== Page Replacement Integration ====================
// Export info to VM layer and swapper/pageout daemon

// Get current page statistics
void vm_page_get_stats(vm_page_stats_t *stats) {
    if (!stats) return;
    
    stats->active_count = 0;
    stats->inactive_count = 0;
    stats->dirty_count = 0;
    stats->free_count = 0;
    
    // Count active pages
    vm_page_t *m = active_queue;
    while (m) {
        stats->active_count++;
        if (m->flags & PG_DIRTY) stats->dirty_count++;
        m = m->next;
    }
    
    // Count inactive pages
    m = inactive_queue;
    while (m) {
        stats->inactive_count++;
        if (m->flags & PG_DIRTY) stats->dirty_count++;
        m = m->next;
    }
    
    // Count free pages
    m = free_queue;
    while (m) {
        stats->free_count++;
        m = m->next;
    }
}

// Estimate working set size (pages actively used)
// Returns count of pages that have been recently accessed
int vm_page_estimate_working_set(void) {
    int count = 0;
    
    // Active pages with age > 0 are part of working set
    vm_page_t *m = active_queue;
    while (m) {
        if (m->age > 0) count++;
        m = m->next;
    }
    
    // Recently reactivated inactive pages
    m = inactive_queue;
    while (m) {
        if (pmap_is_referenced(m)) count++;  // About to be reactivated
        m = m->next;
    }
    
    return count;
}

// Hint for swapper/pageout daemon: should we start paging out?
// Returns 1 if memory pressure is high
int vm_page_should_pageout(void) {
    vm_page_stats_t stats;
    vm_page_get_stats(&stats);
    
    // Pageout if free pages below target or inactive queue low
    if (stats.free_count < vm_page_free_target) {
        return 1;
    }
    
    // Pageout if too many dirty pages
    if (stats.dirty_count > stats.inactive_count / 2) {
        return 1;
    }
    
    return 0;
}
