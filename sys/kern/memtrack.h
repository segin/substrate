/*
 * memtrack.h — per-call-site physical-page allocation accounting.
 *
 * Every pmm_alloc_block / pmm_alloc_contiguous records the caller's
 * return address; every matching free is attributed back to that
 * same site via an index stamped on the page frame.  The result is a
 * table of "this code path has N pages allocated, M freed, N-M still
 * outstanding" — a direct leak finder.
 *
 * Exposed to userspace through /proc/memtrack and sys_vm_slabs(2).
 */
#ifndef _KERN_MEMTRACK_H
#define _KERN_MEMTRACK_H

#include <stdint.h>
#include <stddef.h>

#define MEMTRACK_SITES 256      /* hash-table capacity; slot 0 = catch-all */

typedef struct memtrack_rec {
    uintptr_t pc;            /* caller return address; 0 = untracked slot */
    uint64_t  pages_alloc;   /* cumulative pages handed out from here */
    uint64_t  pages_free;    /* cumulative pages returned to this site */
} memtrack_rec_t;

/*
 * Record an allocation of `pages` pages requested by caller `pc`.
 * Returns the site index to stamp on the page frame(s) so the
 * eventual free can be charged back to the same site.
 */
uint16_t memtrack_record_alloc(uintptr_t pc, uint32_t pages);

/* Charge a free of `pages` pages against the site `idx` came from. */
void memtrack_record_free(uint16_t idx, uint32_t pages);

/*
 * Copy up to `max` in-use records into `out`.  Returns how many were
 * written.  Safe to call from any context.
 */
size_t memtrack_snapshot(memtrack_rec_t *out, size_t max);

#endif /* _KERN_MEMTRACK_H */
