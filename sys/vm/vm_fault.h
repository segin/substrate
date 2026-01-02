#ifndef _VM_FAULT_H
#define _VM_FAULT_H

#include <stdint.h>
#include "vm_map.h"

// VM Fault Types/Results
#define VM_FAULT_SUCCESS    0
#define VM_FAULT_RETRY      1
#define VM_FAULT_ERROR      -1

// High-level fault handler
// Resolves a page fault at 'va' in the given 'map' with requested 'prot'.
int vm_fault(vm_map_t *map, uintptr_t va, uint8_t prot);

#endif
