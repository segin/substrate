#ifndef _VM_FAULT_H
#define _VM_FAULT_H

#include <stdint.h>
#include <vm/vm_map.h>

// VM Fault Types/Results
#define VM_FAULT_SUCCESS    0
#define VM_FAULT_RETRY      1
#define VM_FAULT_ERROR      -1
/*
 * Physical-resource exhaustion at fault time — no free physical pages
 * (or no free page-table page) to back the faulting VA.  Distinct from
 * VM_FAULT_ERROR so the trap dispatcher can surface a clear OOM
 * diagnostic and pick a more appropriate signal (SIGBUS, not SIGSEGV).
 * Substrate has no swap, so this is fatal to the faulting process —
 * but it is a kernel-resource condition, not a programmer error.
 */
#define VM_FAULT_OOM        -2
/*
 * A reference to a whole page that lies entirely beyond the end of the
 * memory object backing the mapping — e.g. the second page of a two-page
 * mapping of a half-page file/shm object (the object was sized with
 * ftruncate(2) smaller than the mapping length).  POSIX requires such a
 * reference to raise SIGBUS, not SIGSEGV: the address is inside a valid
 * mapping but there is no object data (nor any whole partial page) to
 * fault in.  Distinct from VM_FAULT_ERROR so the trap dispatcher delivers
 * SIGBUS (BUS_ADRERR) rather than SIGSEGV.  (mmap/11-2, 11-3.)
 */
#define VM_FAULT_SIGBUS     -3

// High-level fault handler
// Resolves a page fault at 'va' in the given 'map' with requested 'prot'.
int vm_fault(vm_map_t *map, uintptr_t va, uint8_t prot);

/*
 * May `va` in `map` be written?  For the pmap, before it takes a write to
 * a present read-only page for copy-on-write: a page is read-only in the
 * page table both when it is waiting to be copied and when its mapping
 * simply does not allow writing, and only the map can tell which.  1 if
 * the mapping there allows it, or if the map has nothing recorded for the
 * address (a page the pmap was given directly, which is the pmap's to
 * answer for); 0 if the mapping forbids it.
 */
int vm_map_write_permitted(vm_map_t *map, uintptr_t va);

/* Hand the faulting eip to vm_fault's in-flight record so the re-entrancy
 * report can name it.  Optional: skipping the call only blanks that field. */
void vm_fault_note_pc(uint32_t pc);

#endif
