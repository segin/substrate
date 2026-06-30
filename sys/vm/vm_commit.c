/*
 * vm_commit.c -- strict memory-commit accounting (no overcommit).
 *
 * See vm_commit.h for the model.  The global counter `committed_pages`
 * tracks pages PROMISED to userspace (lazily demand-paged anon mmap +
 * grown brk heap).  The check-and-charge is done under a single spinlock
 * so the invariant `committed <= committable` holds atomically against
 * concurrent chargers on other CPUs.
 */

#include <vm/vm_commit.h>
#include <vm/vm_page.h>
#include <vm/phys_mem.h>
#include <sys/lock.h>

/* Pages currently promised (committed) but not necessarily resident. */
static size_t committed_pages;
static spinlock_t commit_lock = SPINLOCK_INIT("vm_commit");

/*
 * The commit limit in pages.
 *
 *     committable = usable_physical_pages - kernel_reserve + swap
 *
 * usable_physical_pages is every page the buddy allocator ever managed
 * (free + used); kernel_reserve is the page daemon's free-reserved
 * watermark (pages kept back for kernel emergencies, see vm_page.c).
 * Swap is 0 on substrate today.  Computed fresh each call -- the figure
 * is stable after boot, and recomputing avoids an init-ordering hazard.
 */
size_t vm_commit_limit(void) {
    size_t total = vm_phys_get_free() + vm_phys_get_used();

    vm_page_thresholds_t th;
    vm_page_get_thresholds(&th);
    size_t reserve = th.free_reserved;

    if (total <= reserve) {
        return 0;
    }
    return total - reserve;
}

int vm_commit_charge(size_t npages) {
    if (npages == 0) {
        return 0;
    }

    size_t limit = vm_commit_limit();

    unsigned long flags = spinlock_acquire_irq(&commit_lock);
    /* Overflow-safe: limit - committed cannot underflow because the
     * invariant committed <= limit is maintained on every charge. */
    size_t headroom = (committed_pages < limit) ? (limit - committed_pages) : 0;
    if (npages > headroom) {
        spinlock_release_irq(&commit_lock, flags);
        return -1; /* would exceed the commit limit -> ENOMEM */
    }
    committed_pages += npages;
    spinlock_release_irq(&commit_lock, flags);
    return 0;
}

void vm_commit_uncharge(size_t npages) {
    if (npages == 0) {
        return;
    }

    unsigned long flags = spinlock_acquire_irq(&commit_lock);
    if (npages > committed_pages) {
        /* Defensive: an over-uncharge is an accounting bug somewhere.
         * Clamp to 0 rather than wrapping the unsigned counter, which
         * would otherwise wedge the system into permanent false-ENOMEM. */
        committed_pages = 0;
    } else {
        committed_pages -= npages;
    }
    spinlock_release_irq(&commit_lock, flags);
}

size_t vm_commit_current(void) {
    unsigned long flags = spinlock_acquire_irq(&commit_lock);
    size_t v = committed_pages;
    spinlock_release_irq(&commit_lock, flags);
    return v;
}
