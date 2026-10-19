/*
 * memtrack.c — per-call-site physical-page allocation accounting.
 *
 * A fixed open-addressed hash table keyed on the allocating caller's
 * return address.  Every page leaving the buddy allocator
 * (vm_phys_alloc_*) is recorded against its caller and the index stamped
 * on the page frame (pmm_alloc_* re-attribute to their own caller); every
 * page returning to it (vm_phys_free_*, when it is really freed and not
 * just unreferenced) charges the free back to the stamped site.  So the
 * totals balance against the free page count, and the difference
 * (pages_alloc - pages_free) per site is the live page footprint of that
 * code path — a positive, growing delta is a leak.  Hooking only the pmm
 * wrappers missed every page freed or allocated straight through
 * vm_phys (object pages, pmap teardown), so the table could not balance.
 *
 * Slot 0 is the catch-all: a free whose page carries no valid stamp
 * (e.g. memory freed before tracking began) lands there rather than
 * being mis-attributed.
 */

#include <string.h>

#include <kern/memtrack.h>
#include <sys/lock.h>

static memtrack_rec_t   memtrack_sites[MEMTRACK_SITES];
static spinlock_t       memtrack_lock = SPINLOCK_INIT("memtrack");

/* The slot for caller `pc`, claimed if new; 0 (catch-all) for pc 0 or a
 * full table.  The lock is held. */
static uint16_t memtrack_site_locked(uintptr_t pc)
{
    if (!pc) return 0;
    unsigned h = (unsigned)((pc >> 4) % MEMTRACK_SITES);
    for (unsigned probe = 0; probe < MEMTRACK_SITES; probe++) {
        unsigned i = (h + probe) % MEMTRACK_SITES;
        if (i == 0) continue;               /* slot 0 is reserved */
        if (memtrack_sites[i].pc == pc) return (uint16_t)i;
        if (memtrack_sites[i].pc == 0) {
            memtrack_sites[i].pc = pc;
            return (uint16_t)i;
        }
    }
    return 0;                   /* table full of distinct sites */
}

uint16_t memtrack_record_alloc(uintptr_t pc, uint32_t pages)
{
    /* IRQ-save: the netstack frees (and so can allocate) pages from
     * hard IRQ context, so this leaf lock must keep IRQs out. */
    unsigned long f = spinlock_acquire_irq(&memtrack_lock);
    uint16_t idx = memtrack_site_locked(pc);
    memtrack_sites[idx].pages_alloc += pages;
    spinlock_release_irq(&memtrack_lock, f);
    return idx;
}

uint16_t memtrack_retag(uint16_t idx, uintptr_t pc, uint32_t pages)
{
    if (idx >= MEMTRACK_SITES) idx = 0;
    unsigned long f = spinlock_acquire_irq(&memtrack_lock);
    uint16_t to = memtrack_site_locked(pc);
    memtrack_sites[idx].pages_alloc -= pages;
    memtrack_sites[to].pages_alloc += pages;
    spinlock_release_irq(&memtrack_lock, f);
    return to;
}

void memtrack_record_free(uint16_t idx, uint32_t pages)
{
    if (idx >= MEMTRACK_SITES) idx = 0;
    unsigned long f = spinlock_acquire_irq(&memtrack_lock);
    memtrack_sites[idx].pages_free += pages;
    spinlock_release_irq(&memtrack_lock, f);
}

size_t memtrack_snapshot(memtrack_rec_t *out, size_t max)
{
    if (!out || max == 0) return 0;
    unsigned long f = spinlock_acquire_irq(&memtrack_lock);
    size_t n = 0;
    for (unsigned i = 0; i < MEMTRACK_SITES && n < max; i++) {
        if (memtrack_sites[i].pc != 0 ||
            memtrack_sites[i].pages_alloc != 0 ||
            memtrack_sites[i].pages_free != 0)
            out[n++] = memtrack_sites[i];
    }
    spinlock_release_irq(&memtrack_lock, f);
    return n;
}
