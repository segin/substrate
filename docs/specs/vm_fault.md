# VM Fault Handler Specification

## Overview
The `vm_fault` function is the high-level entry point for resolving page faults. it bridges the abstract VM Map/Object layers with the hardware-specific PMAP layer.

## Design
- **Trigger:** Invoked when the CPU triggers a page fault exception (typically from the IDT handler).
- **Resolution Strategy:**
    1. Identify the `vm_map_entry` covering the faulting address.
    2. Verify the requested access against the entry's protection flags.
    3. Calculate the object index (pindex).
    4. Retrieve or allocate the `vm_page_t` from the backing `vm_object`.
    5. Instruct the PMAP layer to create the hardware mapping.

## API
### `int vm_fault(vm_map_t *map, uintptr_t va, uint8_t prot)`
- `map`: The virtual address space where the fault occurred.
- `va`: The virtual address that caused the fault.
- `prot`: The type of access requested (Read/Write/Exec).
- **Returns:** `VM_FAULT_SUCCESS` on success, or `VM_FAULT_ERROR`.

## Constraints
- Assumes recursive paging is configured in the PMAP.
- Currently lacks support for paging to/from disk (swap).
- Lacks Copy-on-Write (COW) implementation in the current logic.
