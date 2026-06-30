#ifndef _VM_COMMIT_H
#define _VM_COMMIT_H

#include <stdint.h>
#include <stddef.h>

/*
 * Strict memory-commit accounting (no overcommit).
 *
 * A page is "committed" when the kernel has PROMISED to back it with a
 * physical frame even though it may not be resident yet (a lazily
 * demand-paged anonymous mmap, or a grown brk heap).  The commit limit is
 *
 *     committable = usable_physical_pages - kernel_reserve + swap
 *
 * and the module enforces the invariant `committed <= committable`, so every
 * charged page is guaranteed to be backable at fault time.  Substrate has no
 * swap today, so swap contributes 0.
 *
 * The accessor pair vm_commit_charge()/vm_commit_uncharge() is the SOLE way
 * to mutate the global counter.  Charge at the request sites (anonymous
 * private mmap, brk grow); uncharge at the release sites (munmap, brk shrink,
 * exec teardown, process exit).  Charge and uncharge MUST balance exactly --
 * a leak would wedge the system into false-ENOMEM over time.
 */

/*
 * Try to reserve `npages` pages of commit.  Returns 0 on success (counter
 * incremented), or -1 (ENOMEM) if the charge would exceed the commit limit
 * (counter left unchanged).  Charging 0 pages always succeeds and is a no-op.
 */
int vm_commit_charge(size_t npages);

/*
 * Release `npages` pages of commit previously reserved with
 * vm_commit_charge().  Never fails.  Clamps at 0 (defensive: an
 * over-uncharge is a bug, but we refuse to underflow the counter).
 */
void vm_commit_uncharge(size_t npages);

/* Current number of committed (promised) pages. */
size_t vm_commit_current(void);

/* The commit limit in pages: usable RAM - kernel reserve + swap. */
size_t vm_commit_limit(void);

#endif /* _VM_COMMIT_H */
