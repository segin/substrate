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

// High-level fault handler
// Resolves a page fault at 'va' in the given 'map' with requested 'prot'.
int vm_fault(vm_map_t *map, uintptr_t va, uint8_t prot);

#endif
