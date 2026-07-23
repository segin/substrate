#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vm_object.h>
#include <vm/vm_kmem.h>
#include <vm/uma.h>
#include <arch/i386/pmap.h>
#include <arch/i386/pmm.h>
#include <drivers/console/console.h>
#include <vfs/buf.h>
#include <kern/time.h>
#include <kern/sched.h>
#include <pm/pm.h>
#include <sys/param.h>
#include <sys/signal.h>
#include <stddef.h>
#include <string.h>
#include <vm/vm_area.h>
#include <vm/phys_mem.h>
#include <vm/vm_map.h>
#include <sys/lock.h>

// System Page Queues
static vm_page_t *active_queue = NULL;
static vm_page_t *inactive_queue = NULL;
static vm_page_t *wired_queue = NULL;      // Pages pinned in memory (kernel, DMA)
static vm_page_t *laundry_queue = NULL;    // Dirty pages pending writeback

/*
 * Serialises the four global page queues and the enqueue()/dequeue() splices
 * (VM-03).  The queues are mutated concurrently by the fault path
 * (vm_page_activate), the exit path (vm_page_free), wiring, and the
 * pagedaemon scanners; without this an interleaved pair of splices corrupts
 * the next/prev linkage.
 *
 * IRQ-safe (spinlock_acquire_irq): reachable from IRQ-context frees.
 * LEAF lock — held only across the pointer splices + the PG_ACTIVE/PG_INACTIVE
 * flag update that selects a page's queue.  Never call kmalloc/pmm/pmap/pager/
 * sleep while holding it (in particular vm_page_free / vm_pager_* / the pmap
 * A-bit probes run with it dropped), and it is never taken under another lock
 * that is itself taken under it.
 */
static spinlock_t vm_page_queue_lock = SPINLOCK_INIT("vm_page_queue");

/*
 * Protects the static pv_entry free list and its one-shot bootstrap (VM-03).
 * IRQ-safe and a LEAF lock: the kmalloc() fallback in pv_alloc() runs with it
 * dropped, so it never nests kmalloc/UMA locks under itself.  It guards only
 * the pool free list — the per-page pv_list chains remain serialised by the
 * caller's pmap lock (pmap.c), which never takes this lock, so no cycle.
 */
static spinlock_t vm_pv_lock = SPINLOCK_INIT("vm_pv");

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
static vm_page_policy_t vm_page_policy = VM_PAGE_POLICY_CLOCK;
static int vm_page_daemon_suspended = 0;

void vm_page_init(void) {
	// Initialize queues
	active_queue = NULL;
	inactive_queue = NULL;
	wired_queue = NULL;
	laundry_queue = NULL;
}

static int queue_has_cycle(vm_page_t *head) {
	vm_page_t *slow = head;
	vm_page_t *fast = head;

	while (fast && fast->next) {
		slow = slow->next;
		fast = fast->next->next;
		if (slow == fast) {
			return 1;
		}
	}

	return 0;
}

static int queue_contains(vm_page_t *head, vm_page_t *target) {
	for (vm_page_t *m = head; m; m = m->next) {
		if (m == target) {
			return 1;
		}
	}
	return 0;
}

static int vm_page_process_has_live_threads(process_t *proc) {
	if (!proc) {
		return 0;
	}

    FOREACH_THREAD(thread) {
        if (thread->proc != proc) {
            continue;
        }
        if (thread->state != THREAD_ZOMBIE) {
            return 1;
        }
	}

	return 0;
}

static uint32_t vm_page_oom_score(process_t *proc) {
	uint32_t score = 0;

	if (!proc) {
		return 0;
	}

	if (proc->pmap && proc->pmap != pmap_kernel()) {
		score = proc->pmap->resident_count;
	}

	if (score == 0 && proc->vm_map) {
		score = (uint32_t)((proc->vm_map->size + 4095) / 4096);
	}

	if (score == 0) {
		for (struct vm_area *area = proc->vm_areas; area; area = area->next) {
			if (area->vm_end > area->vm_start) {
				score += (uint32_t)(((uintptr_t)area->vm_end - (uintptr_t)area->vm_start + 4095) / 4096);
			}
		}
	}

	if (score == 0) {
		score = 1;
	}

	return score;
}

process_t *vm_page_select_oom_victim(void) {
	process_t *victim = NULL;
	uint32_t best_score = 0;

    FOREACH_PROC(proc) {
        uint32_t score;

        if (proc->pid <= 1) {
            continue;
        }
		if (proc->is_kernel_task) {
			continue;
		}
		if (proc->state == SDYING || proc->state == SZOMB) {
			continue;
		}
		if (!vm_page_process_has_live_threads(proc)) {
			continue;
		}

		score = vm_page_oom_score(proc);

		/* Re-check process validity after scoring (concurrent exit defense) */
		if (!proc || proc->pid <= 1 || proc->state == SDYING || proc->state == SZOMB) {
			continue;
		}

		if (!victim || score > best_score ||
		    (score == best_score && proc->pid > victim->pid)) {
			victim = proc;
			best_score = score;
		}
	}

	return victim;
}

int vm_page_oom_kill(void) {
	process_t *victim = vm_page_select_oom_victim();
	uint32_t score;

	if (!victim) {
		return 0;
	}

	score = vm_page_oom_score(victim);
	kprintf("OOM: killing pid %d (%s), score=%u pages\n",
	    victim->pid, victim->comm, score);
	psignal(victim, SIGKILL);
	return 1;
}

int vm_page_check_queues(void) {
	if (queue_has_cycle(active_queue) || queue_has_cycle(inactive_queue) ||
	    queue_has_cycle(wired_queue) || queue_has_cycle(laundry_queue)) {
		return 0;
	}

	for (vm_page_t *m = active_queue; m; m = m->next) {
		if (!(m->flags & PG_ACTIVE) || (m->flags & PG_INACTIVE)) return 0;
		if (m->prev && m->prev->next != m) return 0;
		if (m->next && m->next->prev != m) return 0;
		if (queue_contains(inactive_queue, m) || queue_contains(wired_queue, m) ||
		    queue_contains(laundry_queue, m)) return 0;
	}

	for (vm_page_t *m = inactive_queue; m; m = m->next) {
		if (!(m->flags & PG_INACTIVE) || (m->flags & PG_ACTIVE)) return 0;
		if (m->prev && m->prev->next != m) return 0;
		if (m->next && m->next->prev != m) return 0;
		if (queue_contains(active_queue, m) || queue_contains(wired_queue, m) ||
		    queue_contains(laundry_queue, m)) return 0;
	}

	for (vm_page_t *m = wired_queue; m; m = m->next) {
		if (m->wire_count == 0) return 0;
		if (m->flags & (PG_ACTIVE | PG_INACTIVE)) return 0;
		if (m->prev && m->prev->next != m) return 0;
		if (m->next && m->next->prev != m) return 0;
		if (queue_contains(active_queue, m) || queue_contains(inactive_queue, m) ||
		    queue_contains(laundry_queue, m)) return 0;
	}

	for (vm_page_t *m = laundry_queue; m; m = m->next) {
		if (m->prev && m->prev->next != m) return 0;
		if (m->next && m->next->prev != m) return 0;
		if (queue_contains(active_queue, m) || queue_contains(inactive_queue, m) ||
		    queue_contains(wired_queue, m)) return 0;
	}

	return 1;
}

void vm_page_set_policy(vm_page_policy_t policy) {
	if (policy != VM_PAGE_POLICY_CLOCK &&
	    policy != VM_PAGE_POLICY_LRU_APPROX) {
		return;
	}

	vm_page_policy = policy;
}

vm_page_policy_t vm_page_get_policy(void) {
	return vm_page_policy;
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
		uint64_t deadline = get_ticks() + get_hz();
		sched_sleep_until((void *)&vm_pages_needed, deadline);
		if (vm_page_daemon_suspended) {
			sched_yield();
			continue;
		}
		if (vm_page_policy == VM_PAGE_POLICY_LRU_APPROX) {
			vm_page_age_scan();
		}
		if (vm_pages_needed || vm_page_should_pageout()) {
			vm_pageout();
		}
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

// Internal helper to add a page to the head of a queue.
// Caller MUST hold vm_page_queue_lock (raw splice, no internal locking).
static void enqueue(vm_page_t **head, vm_page_t *page) {
	page->next = *head;
	page->prev = NULL;
	if(*head) {
		(*head)->prev = page;
	}
	*head = page;
}

// Internal helper to remove a page from a queue.
// Caller MUST hold vm_page_queue_lock (raw splice, no internal locking).
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

static int pv_entry_ptr_sane(const struct pv_entry *entry) {
    uintptr_t addr = (uintptr_t)entry;

    if (!entry) {
        return 1;
    }

    if (addr < KERN_BASE) {
        return 0;
    }

    if (addr >= 0xFF000000U) {
        return 0;
    }

    if (addr & (sizeof(void *) - 1)) {
        return 0;
    }

    return 1;
}

static void pv_pool_init(void) {
	if(pv_pool_initialized) return;
	for(int i = 0; i < PV_POOL_SIZE; i++) {
		pv_pool[i].next = pv_free_list;
		pv_free_list = &pv_pool[i];
	}
	pv_pool_initialized = 1;
}

static struct pv_entry *pv_alloc(void) {
	unsigned long f = spinlock_acquire_irq(&vm_pv_lock);
	if(!pv_pool_initialized)
		pv_pool_init();          /* links the static array only — no external calls */

	if(pv_free_list) {
		struct pv_entry *entry = pv_free_list;
		pv_free_list = entry->next;
		spinlock_release_irq(&vm_pv_lock, f);
		entry->next = NULL;
		entry->pmap = NULL;
		entry->va = 0;
		entry->poison = PV_POISON_LIVE;
		return(entry);
	}
	spinlock_release_irq(&vm_pv_lock, f);

	// Fallback to kmalloc.  Leaf discipline: NOT under vm_pv_lock (kmalloc
	// takes the kmem/UMA locks, which must never nest under this lock).
	struct pv_entry *entry = kmalloc(sizeof(struct pv_entry));
	if(entry) {
		entry->next = NULL;
		entry->pmap = NULL;
		entry->va = 0;
		entry->poison = PV_POISON_LIVE;
	}
	return(entry);
}

/* Count of pv double-frees caught by the poison guard (diagnostic). */
volatile uint32_t pv_double_free_count = 0;

static void pv_free(struct pv_entry *entry) {
	if(!entry) return;

	/*
	 * Double-free diagnostic.  A pv_entry freed twice means the same
	 * mapping node reached pv_free via two paths (the pv_list SMP race:
	 * pv_remove_all frees before detaching page->pv_list, so a concurrent
	 * pv_alloc on another CPU can re-hand-out a still-linked node).  Catch
	 * it HERE — with the mapping's pmap/va and the offending caller — and
	 * SURVIVE (log + skip the second free) so one workload run enumerates
	 * every occurrence instead of panicking on the first in uma_zfree.
	 */
	if (entry->poison == PV_POISON_FREED) {
		__sync_fetch_and_add(&pv_double_free_count, 1);
		kprintf("PV-DOUBLE-FREE #%u: entry=%p pmap=%p va=%p "
		        "caller=%p (skipped 2nd free)\n",
		        (unsigned)pv_double_free_count, (void *)entry,
		        (void *)entry->pmap, (void *)entry->va,
		        __builtin_return_address(0));
		return;   /* do NOT free again — avoids the uma double-free panic */
	}
	entry->poison = PV_POISON_FREED;

	// Return to static pool if it came from there
	if(entry >= &pv_pool[0] && entry < &pv_pool[PV_POOL_SIZE]) {
		unsigned long f = spinlock_acquire_irq(&vm_pv_lock);
		entry->next = pv_free_list;
		pv_free_list = entry;
		spinlock_release_irq(&vm_pv_lock, f);
	} else {
		kfree(entry, sizeof(struct pv_entry));
	}
}

void pv_insert(vm_page_t *page, struct pmap *pmap, uintptr_t va) {
	if(!page || !pmap) return;

	if (!pv_entry_ptr_sane(page->pv_list)) {
		page->pv_list = NULL;
	}

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

	if (!pv_entry_ptr_sane(page->pv_list)) {
		page->pv_list = NULL;
		return;
	}

	struct pv_entry **pp = &page->pv_list;
	while(*pp) {
		if (!pv_entry_ptr_sane(*pp)) {
			*pp = NULL;
			return;
		}
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

	if (!pv_entry_ptr_sane(page->pv_list)) {
		page->pv_list = NULL;
		return;
	}

	struct pv_entry *entry = page->pv_list;
	while(entry) {
		if (!pv_entry_ptr_sane(entry)) {
			page->pv_list = NULL;
			return;
		}
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

	// Initialize page (clear list pointers to detached state so we can
	// safely free the page even if it was never linked into an object,
	// without corrupting that object's pages list).
	page->object = object;
	page->pindex = pindex;
	page->ref_count = 1;
	page->flags |= PG_BUSY;
	page->next = NULL;
	page->prev = NULL;
	page->obj_next = NULL;
	page->obj_prev = NULL;

	return(page);
}

void vm_page_free(vm_page_t *m) {
	if(!m) return;

	/* Validate magic canary - detects corruption and invalid pointers */
	if(!vm_page_valid(m)) {
		kprint("vm_page_free: corrupted page detected (magic canary invalid)!\n");
		return;
	}

	// Remove from whatever queue it's in (atomic with the flag clear so a
	// concurrent scanner/mutator can't double-splice this page).
	unsigned long qf = spinlock_acquire_irq(&vm_page_queue_lock);
	if(m->flags & PG_ACTIVE) {
		dequeue(&active_queue, m);
		m->flags &= ~PG_ACTIVE;
	} else if(m->flags & PG_INACTIVE) {
		dequeue(&inactive_queue, m);
		m->flags &= ~PG_INACTIVE;
	}
	spinlock_release_irq(&vm_page_queue_lock, qf);

	// Unlink from owning object (outside the queue lock — leaf discipline).
	if(m->object)
		vm_page_remove(m);

	// Remove all pv_entry backlinks
	pv_remove_all(m);

	// Reset state.
	//
	// Do NOT set PG_FREE here.  vm_phys_free_page() interprets PG_FREE
	// as "this page is already on the buddy free list" and early-returns
	// to guard against double-free.  Setting it before the call means
	// the page never actually reaches vm_phys_buddy_enqueue() — the
	// counter at vm_phys_free_count is silently never incremented and
	// the physical memory is permanently lost from the allocator's
	// view.  vm_phys_buddy_enqueue() will set PG_FREE itself once the
	// page is actually on the free list.
	m->ref_count = 0;
	m->wire_count = 0;
	m->access_count = 0;
	m->age = 0;
	/* Preserve PG_PMM_ALLOC so vm_phys_free_page's free-of-unallocated
	 * tripwire still sees the buddy-allocator state.  Also preserve PG_FREE
	 * (A52): if this page is already sitting on the buddy free list, PG_FREE
	 * is set — stripping it here would let the double-free sail past
	 * vm_phys_free_page()'s PG_FREE early-return and either panic
	 * ('free of unallocated page') or, if the frame was meanwhile
	 * re-allocated, buddy-free a live frame.  Keeping PG_FREE lets the
	 * double-free guard fire. */
	m->flags &= (PG_PMM_ALLOC | PG_FREE);

	// Return to generic PMM (Buddy Allocator Coalescing)
	vm_phys_free_page(m);
}

// Move page to active queue (recently accessed)
void vm_page_activate(vm_page_t *m) {
	if(!m) return;

	unsigned long f = spinlock_acquire_irq(&vm_page_queue_lock);
	if(m->flags & PG_ACTIVE) {
		spinlock_release_irq(&vm_page_queue_lock, f);
		return;
	}

	// Remove from current queue
	if(m->flags & PG_INACTIVE) {
		dequeue(&inactive_queue, m);
		m->flags &= ~PG_INACTIVE;
	}

	// Add to active queue
	enqueue(&active_queue, m);
	m->flags |= PG_ACTIVE;
	if (m->age < VM_PAGE_AGE_INITIAL) {
		m->age = VM_PAGE_AGE_INITIAL;
	}
	spinlock_release_irq(&vm_page_queue_lock, f);
}

// Move page to inactive queue (eviction candidate)
void vm_page_deactivate(vm_page_t *m) {
	if(!m) return;

	unsigned long f = spinlock_acquire_irq(&vm_page_queue_lock);
	if(m->flags & PG_INACTIVE) {
		spinlock_release_irq(&vm_page_queue_lock, f);
		return;
	}

	// Remove from active queue
	if(m->flags & PG_ACTIVE) {
		dequeue(&active_queue, m);
		m->flags &= ~PG_ACTIVE;
	}

	enqueue(&inactive_queue, m);
	m->flags |= PG_INACTIVE;
	spinlock_release_irq(&vm_page_queue_lock, f);
}

// Wire page (pin in memory, cannot be paged out)
void vm_page_wire(vm_page_t *m) {
	if(!m) return;

	unsigned long f = spinlock_acquire_irq(&vm_page_queue_lock);
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
	spinlock_release_irq(&vm_page_queue_lock, f);
}

// Unwire page (allow paging out when wire_count reaches 0)
void vm_page_unwire(vm_page_t *m) {
	if(!m) return;

	unsigned long f = spinlock_acquire_irq(&vm_page_queue_lock);
	if(m->wire_count == 0) {
		spinlock_release_irq(&vm_page_queue_lock, f);
		return;
	}

	m->wire_count--;

	// Move back to active queue when fully unwired
	if(m->wire_count == 0) {
		dequeue(&wired_queue, m);
		enqueue(&active_queue, m);
		m->flags |= PG_ACTIVE;
	}
	spinlock_release_irq(&vm_page_queue_lock, f);
}

// Hold page (increment ref_count for mapping)
//
// ATOMIC: the same physical frame's ref_count is mutated concurrently by
// fork (pmap_fork -> vm_page_hold on every shared COW page) and by exit
// (pmap_destroy -> vm_page_unhold) across unrelated processes.  On a single
// CPU these interleave when a timer preempt lands mid read-modify-write; on
// SMP they race outright.  A plain `m->ref_count++/--` is a non-atomic RMW:
// under massive concurrent fork/exit (OPTS shm_open/23-1 forks 1000 COW-
// sharing children) a lost update drops the count below the number of live
// mappings, so pmap_destroy's `pv_list==NULL && ref_count==1` gate fires
// while the frame is STILL mapped elsewhere -> vm_page_free() -> the frame is
// handed back to the buddy allocator and reused as a page table / kernel
// object while a stale PTE still points at it -> silent memory corruption ->
// wild control flow -> unhandled kernel exception whose panic path re-faults
// on the corrupted state -> triple fault (hard reset, no message).  Making
// the count atomic keeps it exactly equal to the live-mapping count so the
// free gate can never free a mapped frame.
void vm_page_hold(vm_page_t *m) {
	if(!m) return;
	__sync_fetch_and_add(&m->ref_count, 1);
}

// Unhold page (decrement ref_count).  Saturating atomic decrement: never
// underflow the uint16_t (a wrap to 65535 would strand the frame forever).
// See vm_page_hold() for why this must be atomic.
void vm_page_unhold(vm_page_t *m) {
	if(!m) return;
	uint16_t old;
	do {
		old = m->ref_count;
		if(old == 0) return;
	} while(!__sync_bool_compare_and_swap(&m->ref_count, old, (uint16_t)(old - 1)));
	// Note: Page is freed by vm_page_free() when no longer needed
	// ref_count reaching 0 indicates no active mappings, but page may
	// still be cached in inactive queue for potential reuse
}

static int vm_pageout_scan_clock(int max_scan) {
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

		// Check if page was recently accessed via PTE A bit (pmap probe
		// stays OUTSIDE the queue lock — leaf discipline).
		int referenced = pmap_page_is_referenced(m);
		if(referenced) {
			pmap_page_clear_reference(m);
		}

		// Splice under the lock, re-validating the page is still on the
		// active queue (a concurrent free/mutator may have moved it).
		unsigned long f = spinlock_acquire_irq(&vm_page_queue_lock);
		if(!(m->flags & PG_ACTIVE)) {
			spinlock_release_irq(&vm_page_queue_lock, f);
			m = next;
			continue;
		}
		if(referenced) {
			// Page was accessed - give it second chance (move to head).
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
		spinlock_release_irq(&vm_page_queue_lock, f);

		m = next;
	}

	return(deactivated);
}

static int vm_pageout_scan_lru(int max_scan) {
	int scanned = 0;
	int deactivated = 0;
	vm_page_t *m = active_queue;

	while (m && scanned < max_scan) {
		vm_page_t *next = m->next;
		scanned++;

		if (m->wire_count > 0) {
			m = next;
			continue;
		}

		int referenced = pmap_page_is_referenced(m);
		if (referenced) {
			pmap_page_clear_reference(m);
		}

		unsigned long f = spinlock_acquire_irq(&vm_page_queue_lock);
		if (!(m->flags & PG_ACTIVE)) {
			spinlock_release_irq(&vm_page_queue_lock, f);
			m = next;
			continue;
		}
		if (referenced) {
			m->age = VM_PAGE_AGE_MAX;
			dequeue(&active_queue, m);
			enqueue(&active_queue, m);
			m->flags |= PG_ACTIVE;
		} else if (m->age > 1) {
			m->age--;
		} else {
			m->age = 0;
			dequeue(&active_queue, m);
			m->flags &= ~PG_ACTIVE;
			enqueue(&inactive_queue, m);
			m->flags |= PG_INACTIVE;
			deactivated++;
		}
		spinlock_release_irq(&vm_page_queue_lock, f);

		m = next;
	}

	return deactivated;
}

// Active queue scanner: CLOCK by default, or age-based LRU approximation when selected.
int vm_pageout_scan(int max_scan) {
	if (vm_page_policy == VM_PAGE_POLICY_LRU_APPROX) {
		return vm_pageout_scan_lru(max_scan);
	}

	return vm_pageout_scan_clock(max_scan);
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

static int vm_page_reclaim_inactive_clean(int target) {
	int freed = 0;

	// Read the queue head and every ->next link under the queue lock (A53):
	// vm_page_t.next/prev double as the buddy allocator's free-list links,
	// so loading m->next while another CPU frees m would redirect the walk
	// into buddy-owned memory.  The lock is dropped only to call
	// vm_page_free (which re-takes it — holding it would self-deadlock and
	// it also calls pmm), then re-acquired to advance.
	unsigned long f = spinlock_acquire_irq(&vm_page_queue_lock);
	vm_page_t *m = inactive_queue;
	while(m && freed < target) {
		vm_page_t *next = m->next;

		if((m->flags & PG_INACTIVE) && !(m->flags & PG_DIRTY) &&
		   !(m->flags & PG_BUSY) && m->wire_count == 0) {
			dequeue(&inactive_queue, m);
			m->flags &= ~PG_INACTIVE;
			spinlock_release_irq(&vm_page_queue_lock, f);

			if(vm_page_try_to_free(m)) {
				freed++;
			}

			f = spinlock_acquire_irq(&vm_page_queue_lock);
		}

		m = next;
	}
	spinlock_release_irq(&vm_page_queue_lock, f);

	return(freed);
}

static int vm_page_launder_inactive_dirty(int target) {
	int freed = 0;

	// Hold the queue lock across the head and ->next loads (A53); drop it
	// only for vm_page_launder / vm_page_free, which take it themselves.
	unsigned long f = spinlock_acquire_irq(&vm_page_queue_lock);
	vm_page_t *m = inactive_queue;
	while(m && freed < target) {
		vm_page_t *next = m->next;

		if((m->flags & PG_DIRTY) && !(m->flags & PG_BUSY)) {
			spinlock_release_irq(&vm_page_queue_lock, f);

			vm_page_launder(m);
			if(vm_page_try_to_free(m)) {
				freed++;
			}

			f = spinlock_acquire_irq(&vm_page_queue_lock);
		}

		m = next;
	}
	spinlock_release_irq(&vm_page_queue_lock, f);

	return(freed);
}

// Move dirty page to laundry queue for writeback
void vm_page_launder(vm_page_t *m) {
	if(!m || !(m->flags & PG_DIRTY)) return;

	// Move inactive -> laundry under the queue lock.
	unsigned long f = spinlock_acquire_irq(&vm_page_queue_lock);
	if(m->flags & PG_INACTIVE) {
		dequeue(&inactive_queue, m);
		m->flags &= ~PG_INACTIVE;
	}
	enqueue(&laundry_queue, m);
	spinlock_release_irq(&vm_page_queue_lock, f);

	// Perform writeback via pager (OUTSIDE the queue lock — pager I/O may
	// sleep; holding a leaf spinlock across it is forbidden).
	bool success = false;
	if(m->object && m->object->pager) {
		if(vm_pager_put_pages(m->object->pager, &m, 1, true) == 0) {
			success = true;
		}
	} else if(m->object && m->object->type == VM_OBJ_TYPE_DEFAULT) {
		// Anonymous memory without pager yet?
		// Should have been assigned a swap pager.
	}

	// Move laundry -> inactive (clean) or -> active (retry) under the lock.
	f = spinlock_acquire_irq(&vm_page_queue_lock);
	dequeue(&laundry_queue, m);
	if(success) {
		// Mark clean, move to inactive queue (next scan can free it).
		m->flags &= ~PG_DIRTY;
		enqueue(&inactive_queue, m);
		m->flags |= PG_INACTIVE;
	} else {
		// Failed to swap out (no swap space?)
		// Reactivate it to avoid tight loop of failing laundry
		enqueue(&active_queue, m);
		m->flags |= PG_ACTIVE;
	}
	spinlock_release_irq(&vm_page_queue_lock, f);

	if(success) {
		vm_stat_pageouts++;
	}
}

// Main pageout daemon loop
// Called periodically or when vm_pages_needed is set
void vm_pageout(void) {
	size_t free_count = vm_phys_get_free();

	// Check if we need to free pages
	if(free_count >= (size_t)vm_page_free_target && !vm_pages_needed) {
		return;  // Plenty of free memory
	}

	int freed = 0;
	int target = vm_page_free_target - (int)free_count;
	if(target < 0) target = 0;

	// Phase 0: Reclaim kernel memory (UMA, etc.)
	uma_reclaim();

	// Phase 1: reclaim already-inactive clean pages first.
	freed += vm_page_reclaim_inactive_clean(target - freed);

	// Phase 2: launder dirty inactive pages before disturbing active pages.
	if(freed < target) {
		freed += vm_page_launder_inactive_dirty(target - freed);
	}

	// Phase 3: if we still need headroom, age active pages into inactive.
	if(freed < target) {
		int scan_target = (target - freed) * 2;
		if (scan_target < 1) scan_target = 1;
		vm_pageout_scan(scan_target);
	}

	// Phase 4: retry reclaim on the pages moved in phase 3.
	if(freed < target) {
		freed += vm_page_reclaim_inactive_clean(target - freed);
	}
	if(freed < target) {
		freed += vm_page_launder_inactive_dirty(target - freed);
	}

	// Phase 4.5: still short after reclaiming process pages — drop clean
	// block-cache buffers.  They are pure cache and always safe to release,
	// so the cache gives RAM back under pressure (and a process is not
	// OOM-killed for cache it is not using).  uma_reclaim() then returns the
	// freed buffer memory from the UMA free lists to the page allocator so
	// the phase-5 free-memory check below actually sees it.
	if(freed < target) {
		size_t want = (size_t)(target - freed) * PAGE_SIZE;
		size_t got = bio_reclaim(want);
		if(got) {
			uma_reclaim();
			kprintf("vm: reclaimed %u KiB block cache under memory pressure\n",
			    (unsigned)(got / 1024));
		}
	}

	// Clear request
	vm_pages_needed = 0;

	// Panic if critically low
	if(vm_phys_get_free() < (size_t)vm_page_free_min) {
		if (!vm_page_oom_kill()) {
			kprint("PANIC: Out of memory!\n");
		} else {
			vm_pages_needed = 1;
		}
	}
}

// Signal that pages are needed
void vm_page_wakeup_daemon(void) {
	if (vm_page_daemon_suspended) {
		return;
	}
	vm_pages_needed = 1;
	if (vm_pagedaemon_started) {
		sched_wakeup((void *)&vm_pages_needed);
	} else {
		swapper_request_work();
	}
}

void vm_page_set_daemon_suspended(int suspended) {
	vm_page_daemon_suspended = suspended ? 1 : 0;
	if (vm_page_daemon_suspended) {
		vm_pages_needed = 0;
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
	// Scan active queue - pages accessed get max age, others decrement.
	// The queue head and every ->next link are read under the queue lock
	// (A53) — vm_page_t.next aliases the buddy free-list link, so an unlocked
	// load of m->next racing a concurrent free would walk into buddy memory.
	// The lock is dropped only for the pmap A-bit probe (leaf discipline).
	unsigned long f = spinlock_acquire_irq(&vm_page_queue_lock);
	vm_page_t *m = active_queue;
	while(m) {
		vm_page_t *next = m->next;

		// Skip wired pages
		if(m->wire_count > 0) {
			m = next;
			continue;
		}

		spinlock_release_irq(&vm_page_queue_lock, f);
		int referenced = pmap_page_is_referenced(m);
		if(referenced) {
			pmap_page_clear_reference(m);
		}
		f = spinlock_acquire_irq(&vm_page_queue_lock);

		if(referenced) {
			// Page was accessed - reset age to max
			m->age = VM_PAGE_AGE_MAX;
		} else if(m->flags & PG_ACTIVE) {
			// Page not accessed - age it and, if aged out, move to inactive.
			if(m->age > 0) {
				m->age--;
			}
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

		spinlock_release_irq(&vm_page_queue_lock, f);
		int referenced = pmap_page_is_referenced(m);
		if(referenced) {
			pmap_page_clear_reference(m);
		}
		f = spinlock_acquire_irq(&vm_page_queue_lock);

		// Check if page was accessed while inactive
		if(referenced) {
			// Reactivate under the queue lock.
			if(m->flags & PG_INACTIVE) {
				dequeue(&inactive_queue, m);
				m->flags &= ~PG_INACTIVE;
				enqueue(&active_queue, m);
				m->flags |= PG_ACTIVE;
				m->age = VM_PAGE_AGE_INITIAL;  // Give a second chance
			}
			vm_stat_reactivations++;
		}

		m = next;
	}
	spinlock_release_irq(&vm_page_queue_lock, f);
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

	// Traverse the queues under the queue lock (A53): the ->next links are
	// spliced by vm_page_free/activate/deactivate under this same lock, and
	// alias the buddy free-list links once a page is freed.
	unsigned long f = spinlock_acquire_irq(&vm_page_queue_lock);

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
	spinlock_release_irq(&vm_page_queue_lock, f);

	// Read true free page count from Buddy Allocator
	stats->free_count = vm_phys_get_free();
}

void vm_page_get_vmstat(vm_vmstat_t *stats) {
	struct pmap_stats pstats;

	if(!stats) return;

	memset(stats, 0, sizeof(*stats));

	// Traverse the page queues under the queue lock (A53) — see
	// vm_page_get_stats().
	unsigned long f = spinlock_acquire_irq(&vm_page_queue_lock);
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
	spinlock_release_irq(&vm_page_queue_lock, f);

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

void vm_page_get_thresholds(vm_page_thresholds_t *thresholds) {
	if (!thresholds) return;

	thresholds->free_reserved = (uint32_t)vm_page_free_reserved;
	thresholds->free_min = (uint32_t)vm_page_free_min;
	thresholds->free_target = (uint32_t)vm_page_free_target;
	thresholds->inactive_target = (uint32_t)vm_page_inactive_target;
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
