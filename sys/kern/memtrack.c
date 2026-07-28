/*
 * memtrack.c — per-call-site physical-page allocation accounting.
 *
 * A fixed open-addressed hash table keyed on the allocating caller's
 * return address.  pmm_alloc_*() looks the caller up here and stamps
 * the returned index onto the page frame; pmm_free_*() reads the
 * stamp back and charges the free to the originating site.  The
 * difference (pages_alloc - pages_free) per site is the live page
 * footprint of that code path — a positive, growing delta is a leak.
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

uint16_t memtrack_record_alloc(uintptr_t pc, uint32_t pages)
{
    /* IRQ-save: the netstack frees (and so can allocate) pages from
     * hard IRQ context, so this leaf lock must keep IRQs out. */
    unsigned long f = spinlock_acquire_irq(&memtrack_lock);
    uint16_t idx = 0;                       /* default: catch-all slot */
    if (pc) {
        unsigned h = (unsigned)((pc >> 4) % MEMTRACK_SITES);
        for (unsigned probe = 0; probe < MEMTRACK_SITES; probe++) {
            unsigned i = (h + probe) % MEMTRACK_SITES;
            if (i == 0) continue;           /* slot 0 is reserved */
            if (memtrack_sites[i].pc == pc) { idx = i; break; }
            if (memtrack_sites[i].pc == 0) {
                memtrack_sites[i].pc = pc;
                idx = i;
                break;
            }
        }
        /* Table full of distinct sites — idx stays 0 (catch-all). */
    }
    memtrack_sites[idx].pages_alloc += pages;
    spinlock_release_irq(&memtrack_lock, f);
    return idx;
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
