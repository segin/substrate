#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vm_object.h>
#include <vm/vm_kmem.h>
#include <vm/uma.h>
#include <arch/i386/pmap.h>
#include <arch/i386/pmm.h>
#include <drivers/console/console.h>
#include <kern/time.h>
#include <kern/sched.h>
#include <pm/pm.h>
#include <stddef.h>
#include <string.h>
#include <vm/phys_mem.h>

// System Page Queues
static vm_page_t *active_queue = NULL;
static vm_page_t *inactive_queue = NULL;
static vm_page_t *wired_queue = NULL;      // Pages pinned in memory (kernel, DMA)
static vm_page_t *laundry_queue = NULL;    // Dirty pages pending writeback

static int vm_pagedaemon_started = 0;
static int vm_page_inactive_target = 128;

// Thresholds (pages)
static int vm_page_free_min = 16;      // Panic below this
static int vm_page_free_target = 64;   // Daemon sleeps above this
static int vm_page_free_reserved = 8;  // Reserved for kernel emergencies

// Statistics exported via /proc/vmstat
static uint32_t vm_stat_pageouts = 0;
static uint32_t vm_stat_pageins = 0;
static uint32_t vm_stat_reactivations = 0;

// Wakeup flag for daemon
static volatile int vm_pages_needed = 0;

void vm_page_init(void) {
	// Initialize queues
	active_queue = NULL;
	inactive_queue = NULL;
	wired_queue = NULL;
	laundry_queue = NULL;
}

static uint32_t count_queue(vm_page_t *head) {
	uint32_t count = 0;
	while (head) {
		count++;
		head = head->next;
	}
	return count;
}

static void vm_page_tune_thresholds(void) {
	size_t total_pages = vm_phys_get_free() + vm_phys_get_used();

	vm_page_free_reserved = 8;
	vm_page_free_min = 16;
	vm_page_free_target = 64;
	vm_page_inactive_target = 128;

	if (total_pages == 0) {
		return;
	}

	{
		int reserved = (int)(total_pages / 512);
		int free_min = (int)(total_pages / 256);
		int free_target = (int)(total_pages / 64);
		int inactive_target = (int)(total_pages / 8);

		if (reserved > vm_page_free_reserved) vm_page_free_reserved = reserved;
		if (free_min > vm_page_free_min) vm_page_free_min = free_min;
		if (free_target > vm_page_free_target) vm_page_free_target = free_target;
		if (inactive_target > vm_page_inactive_target) vm_page_inactive_target = inactive_target;
	}
}

static void vm_pagedaemon(void *arg) {
	(void)arg;

	if (current_process) {
		strncpy(current_process->comm, "pagedaemon", AC_COMM_LEN);
		current_process->comm[AC_COMM_LEN - 1] = '\0';
		current_process->is_kernel_task = 1;
	}
	if (current_thread) {
		current_thread->sched_class = SCHED_TIMESHARE;
		current_thread->priority = 1;
		current_thread->base_priority = 1;
	}

	for (;;) {
		while (!vm_pages_needed) {
			sched_sleep((void *)&vm_pages_needed);
		}
		vm_pageout();
		sched_yield();
	}
}

void vm_page_late_init(void) {
	if (vm_pagedaemon_started) {
		return;
	}

	vm_page_tune_thresholds();
	if (sched_spawn_kernel_process(vm_pagedaemon, NULL) >= 0) {
		vm_pagedaemon_started = 1;
	}
}

// Internal helper to add a page to the head of a queue
static void enqueue(vm_page_t **head, vm_page_t *page) {
	page->next = *head;
	page->prev = NULL;
	if(*head) {
		(*head)->prev = page;
	}
	*head = page;
}

// Internal helper to remove a page from a queue
static void dequeue(vm_page_t **head, vm_page_t *page) {
	if(page->prev) {
		page->prev->next = page->next;
	} else {
		*head = page->next;
	}
	if(page->next) {
		page->next->prev = page->prev;
	}
	page->next = NULL;
	page->prev = NULL;
}

// ==================== PV Entry Management ====================
// Singly-linked list of (pmap, va) pairs per physical page.
// Used for reverse mapping: given a physical page, find all virtual mappings.

#define PV_POOL_SIZE 256
static struct pv_entry pv_pool[PV_POOL_SIZE];
static struct pv_entry *pv_free_list = NULL;
static int pv_pool_initialized = 0;

static void pv_pool_init(void) {
	if(pv_pool_initialized) return;
	for(int i = 0; i < PV_POOL_SIZE; i++) {
		pv_pool[i].next = pv_free_list;
		pv_free_list = &pv_pool[i];
	}
	pv_pool_initialized = 1;
}

static struct pv_entry *pv_alloc(void) {
	if(!pv_pool_initialized)
		pv_pool_init();

	if(pv_free_list) {
		struct pv_entry *entry = pv_free_list;
		pv_free_list = entry->next;
		entry->next = NULL;
		entry->pmap = NULL;
		entry->va = 0;
		return(entry);
	}

	// Fallback to kmalloc
	struct pv_entry *entry = kmalloc(sizeof(struct pv_entry));
	if(entry) {
		entry->next = NULL;
		entry->pmap = NULL;
		entry->va = 0;
	}
	return(entry);
}

static void pv_free(struct pv_entry *entry) {
	if(!entry) return;

	// Return to static pool if it came from there
	if(entry >= &pv_pool[0] && entry < &pv_pool[PV_POOL_SIZE]) {
		entry->next = pv_free_list;
		pv_free_list = entry;
	} else {
		kfree(entry, sizeof(struct pv_entry));
	}
}

void pv_insert(vm_page_t *page, struct pmap *pmap, uintptr_t va) {
	if(!page || !pmap) return;

	struct pv_entry *entry = pv_alloc();
	if(!entry) {
		kprint("pv_insert: out of pv_entry structs!\n");
		return;
	}

	entry->pmap = pmap;
	entry->va = va;
	entry->next = page->pv_list;
	page->pv_list = entry;
}

void pv_remove(vm_page_t *page, struct pmap *pmap, uintptr_t va) {
	if(!page) return;

	struct pv_entry **pp = &page->pv_list;
	while(*pp) {
		struct pv_entry *entry = *pp;
		if(entry->pmap == pmap && entry->va == va) {
			*pp = entry->next;
			pv_free(entry);
			return;
		}
		pp = &entry->next;
	}
}

void pv_remove_all(vm_page_t *page) {
	if(!page) return;

	struct pv_entry *entry = page->pv_list;
	while(entry) {
		struct pv_entry *next = entry->next;
		pv_free(entry);
		entry = next;
	}
	page->pv_list = NULL;
}

// ==================== Ownership Tracking ====================
// Link/unlink pages to/from vm_objects.

void vm_page_insert(vm_page_t *page, struct vm_object *object, uint64_t pindex) {
	if(!page || !object) return;

	page->object = object;
	page->pindex = pindex;
	vm_object_add_page(object, page);
}

void vm_page_remove(vm_page_t *page) {
	if(!page || !page->object) return;

	vm_object_remove_page(page->object, page);
	page->object = NULL;
	page->pindex = 0;
}

// ==================== Page Allocation ====================

vm_page_t *vm_page_alloc(struct vm_object *object, uint64_t pindex, int req) {
	(void)req; // Ignore flags for now

	// 1. Allocate from the generic PMM (Buddy Allocator)
	vm_page_t *page = vm_phys_alloc_page();
	if(!page) return(NULL); // OOM

	// Initialize page
	page->object = object;
	page->pindex = pindex;
	page->ref_count = 1;
	page->flags |= PG_BUSY;

	return(page);
}

void vm_page_free(vm_page_t *m) {
	if(!m) return;

	/* Validate magic canary - detects corruption and invalid pointers */
	if(!vm_page_valid(m)) {
		kprint("vm_page_free: corrupted page detected (magic canary invalid)!\n");
		return;
	}

	// Remove from whatever queue it's in
	if(m->flags & PG_ACTIVE) {
		dequeue(&active_queue, m);
		m->flags &= ~PG_ACTIVE;
	} else if(m->flags & PG_INACTIVE) {
		dequeue(&inactive_queue, m);
		m->flags &= ~PG_INACTIVE;
	}

	// Unlink from owning object
	if(m->object)
		vm_page_remove(m);

	// Remove all pv_entry backlinks
	pv_remove_all(m);

	// Reset state
	m->ref_count = 0;
	m->wire_count = 0;
	m->access_count = 0;
	m->age = 0;
	m->flags = 0; // Clear all flags

	// Return to generic PMM (Buddy Allocator Coalescing)
	vm_phys_free_page(m);
}

// Move page to active queue (recently accessed)
void vm_page_activate(vm_page_t *m) {
	if(!m || (m->flags & PG_ACTIVE)) return;

	// Remove from current queue
	if(m->flags & PG_INACTIVE) {
		dequeue(&inactive_queue, m);
		m->flags &= ~PG_INACTIVE;
	}

	// Add to active queue
	enqueue(&active_queue, m);
	m->flags |= PG_ACTIVE;
}

// Move page to inactive queue (eviction candidate)
void vm_page_deactivate(vm_page_t *m) {
	if(!m || (m->flags & PG_INACTIVE)) return;

	// Remove from active queue
	if(m->flags & PG_ACTIVE) {
		dequeue(&active_queue, m);
		m->flags &= ~PG_ACTIVE;
	}

	enqueue(&inactive_queue, m);
	m->flags |= PG_INACTIVE;
}

// Wire page (pin in memory, cannot be paged out)
void vm_page_wire(vm_page_t *m) {
	if(!m) return;

	m->wire_count++;

	// Remove from LRU queues if being wired for first time
	if(m->wire_count == 1) {
		if(m->flags & PG_ACTIVE) {
			dequeue(&active_queue, m);
			m->flags &= ~PG_ACTIVE;
		} else if(m->flags & PG_INACTIVE) {
			dequeue(&inactive_queue, m);
			m->flags &= ~PG_INACTIVE;
		}
		enqueue(&wired_queue, m);
	}
}

// Unwire page (allow paging out when wire_count reaches 0)
void vm_page_unwire(vm_page_t *m) {
	if(!m || m->wire_count == 0) return;

	m->wire_count--;

	// Move back to active queue when fully unwired
	if(m->wire_count == 0) {
		dequeue(&wired_queue, m);
		enqueue(&active_queue, m);
		m->flags |= PG_ACTIVE;
	}
}

// Hold page (increment ref_count for mapping)
void vm_page_hold(vm_page_t *m) {
	if(!m) return;
	m->ref_count++;
}

// Unhold page (decrement ref_count, free if zero and not wired)
void vm_page_unhold(vm_page_t *m) {
	if(!m || m->ref_count == 0) return;
	m->ref_count--;
	// Note: Page is freed by vm_page_free() when no longer needed
	// ref_count reaching 0 indicates no active mappings, but page may
	// still be cached in inactive queue for potential reuse
}

// LRU scanner: Walk active queue and move unreferenced pages to inactive
// Returns number of pages deactivated
int vm_pageout_scan(int max_scan) {
	int scanned = 0;
	int deactivated = 0;

	vm_page_t *m = active_queue;

	while(m && scanned < max_scan) {
		vm_page_t *next = m->next;  // Save next before potential dequeue
		scanned++;

		// Skip wired pages (shouldn't be on active queue but check anyway)
		if(m->wire_count > 0) {
			m = next;
			continue;
		}

		// Check if page was recently accessed via PTE A bit
		if(pmap_page_is_referenced(m)) {
			// Page was accessed - give it second chance
			pmap_page_clear_reference(m);
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

	return(deactivated);
}

// ==================== Page Daemon ====================
// Background daemon that frees pages when memory is low

// Try to free a clean inactive page
// Returns 1 if page was freed, 0 otherwise
int vm_page_try_to_free(vm_page_t *m) {
	if(!m) return(0);

	// Cannot free dirty pages directly
	if(m->flags & PG_DIRTY) return(0);

	// Cannot free busy pages
	if(m->flags & PG_BUSY) return(0);

	// Cannot free wired pages
	if(m->wire_count > 0) return(0);

	// Free the page back to PMM
	vm_page_free(m);
	return(1);
}

// Move dirty page to laundry queue for writeback
void vm_page_launder(vm_page_t *m) {
	if(!m || !(m->flags & PG_DIRTY)) return;

	// Remove from inactive queue
	if(m->flags & PG_INACTIVE) {
		dequeue(&inactive_queue, m);
		m->flags &= ~PG_INACTIVE;
	}

	// Add to laundry queue
	enqueue(&laundry_queue, m);

	// Perform writeback via pager
	bool success = false;
	if(m->object && m->object->pager) {
		if(vm_pager_put_pages(m->object->pager, &m, 1, true) == 0) {
			success = true;
		}
	} else if(m->object && m->object->type == VM_OBJ_TYPE_DEFAULT) {
		// Anonymous memory without pager yet?
		// Should have been assigned a swap pager.
	}

	// Remove from laundry queue
	dequeue(&laundry_queue, m);

	if(success) {
		// Mark clean
		m->flags &= ~PG_DIRTY;
		vm_stat_pageouts++;

		// Move to inactive queue (now clean, so next scan can free it)
		enqueue(&inactive_queue, m);
		m->flags |= PG_INACTIVE;
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
	size_t free_count = vm_phys_get_free();
	uint32_t inactive_count = count_queue(inactive_queue);

	// Check if we need to free pages
	if(free_count >= (size_t)vm_page_free_target && !vm_pages_needed) {
		return;  // Plenty of free memory
	}

	int freed = 0;
	int target = vm_page_free_target - (int)free_count;
	if(target < 0) target = 0;

	// Phase 0: Reclaim kernel memory (UMA, etc.)
	uma_reclaim();

	// Phase 1: Scan active queue, move cold pages to inactive until we have headroom.
	int scan_target = target * 2;
	if (inactive_count < (uint32_t)vm_page_inactive_target) {
		scan_target += (int)(vm_page_inactive_target - inactive_count);
	}
	if (scan_target < 1) scan_target = 1;
	vm_pageout_scan(scan_target);

	// Phase 2: Try to free clean inactive pages
	vm_page_t *m = inactive_queue;
	while(m && freed < target) {
		vm_page_t *next = m->next;

		if(!(m->flags & PG_DIRTY) && !(m->flags & PG_BUSY) && m->wire_count == 0) {
			dequeue(&inactive_queue, m);
			m->flags &= ~PG_INACTIVE;

			if(vm_page_try_to_free(m)) {
				freed++;
			}
		}

		m = next;
	}

	// Phase 3: Launder dirty pages if still short
	if(freed < target) {
		m = inactive_queue;
		while(m && freed < target) {
			vm_page_t *next = m->next;

			if((m->flags & PG_DIRTY) && !(m->flags & PG_BUSY)) {
				vm_page_launder(m);
				// Try to free now that it's clean
				if(vm_page_try_to_free(m)) {
					freed++;
				}
			}

			m = next;
		}
	}

	// Clear request
	vm_pages_needed = 0;

	// Panic if critically low
	if(vm_phys_get_free() < (size_t)vm_page_free_min) {
		kprint("PANIC: Out of memory!\n");
		// OOM killer would go here
	}
}

// Signal that pages are needed
void vm_page_wakeup_daemon(void) {
	vm_pages_needed = 1;
	if (vm_pagedaemon_started) {
		sched_wakeup((void *)&vm_pages_needed);
	} else {
		swapper_request_work();
	}
}

// ==================== Writeback Tracking ====================
// Track pages that need writeback to swap/file

// Check if page needs writeback to backing store
int vm_page_needs_writeback(vm_page_t *m) {
	if(!m) return(0);

	// Page needs writeback if:
	// - It's marked dirty (PG_DIRTY) and not already being written (PG_WRITEBACK)
	// - OR it's explicitly marked as needing sync (PG_NEEDSYNC)
	if((m->flags & PG_DIRTY) && !(m->flags & PG_WRITEBACK)) {
		return(1);
	}
	if(m->flags & PG_NEEDSYNC) {
		return(1);
	}
	return(0);
}

// Mark page for writeback (sets PG_WRITEBACK, starts I/O)
void vm_page_mark_for_writeback(vm_page_t *m) {
	if(!m) return;

	// Don't start writeback on already writing page
	if(m->flags & PG_WRITEBACK) return;

	// Set writeback in progress
	m->flags |= PG_WRITEBACK;
	m->flags |= PG_BUSY;  // Page is busy during I/O

	// Record time for scheduling (use uptime as monotonic timestamp)
	m->last_modified = (uint32_t)get_uptime();
}

// Complete writeback (called when I/O finishes)
void vm_page_writeback_done(vm_page_t *m) {
	if(!m) return;

	// Clear writeback flags
	m->flags &= ~PG_WRITEBACK;
	m->flags &= ~PG_BUSY;
	m->flags &= ~PG_NEEDSYNC;

	// If no one dirtied the page during writeback, it's now clean
	// (If someone did, PG_DIRTY will still be set from the new write)
}

// ==================== Page Aging Algorithm ====================
// Implements clock/LRU approximation for page replacement

// Periodic scan of all resident pages - decrement age if not accessed
// Called by page daemon periodically
void vm_page_age_scan(void) {
	// Scan active queue - pages accessed get max age, others decrement
	vm_page_t *m = active_queue;
	while(m) {
		vm_page_t *next = m->next;

		// Skip wired pages
		if(m->wire_count > 0) {
			m = next;
			continue;
		}

		// Check A-bit via pmap and update access tracking
		if(pmap_page_is_referenced(m)) {
			// Page was accessed - reset age to max
			m->age = VM_PAGE_AGE_MAX;
			pmap_page_clear_reference(m);
		} else {
			// Page not accessed - decrement age
			if(m->age > 0) {
				m->age--;
			}
			// If age reaches 0, move to inactive queue
			if(m->age == 0) {
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
	while(m) {
		vm_page_t *next = m->next;

		// Skip wired pages
		if(m->wire_count > 0) {
			m = next;
			continue;
		}

		// Check if page was accessed while inactive
		if(pmap_page_is_referenced(m)) {
			// Reactivate
			dequeue(&inactive_queue, m);
			m->flags &= ~PG_INACTIVE;
			enqueue(&active_queue, m);
			m->flags |= PG_ACTIVE;
			m->age = VM_PAGE_AGE_INITIAL;  // Give a second chance
			vm_stat_reactivations++;
			pmap_page_clear_reference(m);
		}

		m = next;
	}
}

// Check if page is a candidate for eviction (age=0, not wired, not busy)
int vm_page_is_evict_candidate(vm_page_t *m) {
	if(!m) return(0);

	// Wired pages cannot be evicted
	if(m->wire_count > 0) return(0);

	// Busy pages cannot be evicted
	if(m->flags & PG_BUSY) return(0);

	// Pages in writeback cannot be evicted
	if(m->flags & PG_WRITEBACK) return(0);

	// Active pages are not candidates (need to age out first)
	if(m->flags & PG_ACTIVE) return(0);

	// Inactive pages with age 0 are primary candidates
	if((m->flags & PG_INACTIVE) && m->age == 0) {
		return(1);
	}

	return(0);
}

// ==================== Page Replacement Integration ====================
// Export info to VM layer and swapper/pageout daemon

// Get current page statistics
void vm_page_get_stats(vm_page_stats_t *stats) {
	if(!stats) return;

	stats->active_count = 0;
	stats->inactive_count = 0;
	stats->dirty_count = 0;
	stats->free_count = 0;

	// Count active pages
	vm_page_t *m = active_queue;
	while(m) {
		stats->active_count++;
		if(m->flags & PG_DIRTY) stats->dirty_count++;
		m = m->next;
	}

	// Count inactive pages
	m = inactive_queue;
	while(m) {
		stats->inactive_count++;
		if(m->flags & PG_DIRTY) stats->dirty_count++;
		m = m->next;
	}

	// Read true free page count from Buddy Allocator
	stats->free_count = vm_phys_get_free();
}

void vm_page_get_vmstat(vm_vmstat_t *stats) {
	struct pmap_stats pstats;

	if(!stats) return;

	memset(stats, 0, sizeof(*stats));

	for(vm_page_t *m = active_queue; m; m = m->next) {
		stats->active_count++;
	}
	for(vm_page_t *m = inactive_queue; m; m = m->next) {
		stats->inactive_count++;
	}
	for(vm_page_t *m = wired_queue; m; m = m->next) {
		stats->wire_count++;
	}
	for(vm_page_t *m = laundry_queue; m; m = m->next) {
		stats->laundry_count++;
	}

	stats->free_count = (uint32_t)vm_phys_get_free();
	stats->pageins = vm_stat_pageins;
	stats->pageouts = vm_stat_pageouts;
	stats->reactivations = vm_stat_reactivations;

	if(sys_pmap_stats(&pstats) == 0) {
		stats->faults = pstats.faults;
		stats->cow_faults = pstats.cow_faults;
		stats->zero_fill_pages = pstats.zero_fills;
	}
}

void vm_page_record_pagein(uint32_t count) {
	vm_stat_pageins += count;
}

// Estimate working set size (pages actively used)
// Returns count of pages that have been recently accessed
int vm_page_estimate_working_set(void) {
	int count = 0;

	// Active pages with age > 0 are part of working set
	vm_page_t *m = active_queue;
	while(m) {
		if(m->age > 0) count++;
		m = m->next;
	}

	// Recently reactivated inactive pages
	m = inactive_queue;
	while(m) {
		if(pmap_page_is_referenced(m)) count++;  // About to be reactivated
		m = m->next;
	}

	return(count);
}

// Hint for swapper/pageout daemon: should we start paging out?
// Returns 1 if memory pressure is high
int vm_page_should_pageout(void) {
	vm_page_stats_t stats;
	vm_page_get_stats(&stats);

	// Pageout if free pages below target
	if(stats.free_count < vm_page_free_target) {
		return(1);
	}

	// Pageout if too many dirty pages
	if(stats.dirty_count > stats.inactive_count / 2) {
		return(1);
	}

	return(0);
}
