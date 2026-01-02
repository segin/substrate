# User Memory System Calls Specification

## Overview
This specification defines the behavior of the core memory management system calls: `mmap`, `munmap`, and `brk`. These calls allow user processes to manage their virtual address space.

## System Calls

### `void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)`
Maps a region of memory into the process's virtual address space.
- **Parameters:**
    - `addr`: Suggested starting address (NULL for kernel choice).
    - `length`: Size of the mapping in bytes.
    - `prot`: Protection flags (`PROT_READ`, `PROT_WRITE`, `PROT_EXEC`).
    - `flags`: Mapping type (`MAP_PRIVATE`, `MAP_SHARED`, `MAP_ANONYMOUS`).
    - `fd`: File descriptor (for file-backed mappings).
    - `offset`: Offset within the file.
- **Returns:** Pointer to the mapped area on success, `MAP_FAILED` on error.

### `int munmap(void *addr, size_t length)`
Removes a mapping from the process's virtual address space.
- **Returns:** 0 on success, -1 on error.

### `void *brk(void *addr)`
Changes the data segment break (end of the heap).
- **Parameters:**
    - `addr`: New break address. If NULL, returns the current break.
- **Returns:** The new break address.

## Design
- **Integration:** These calls interact with the `vm_map` and `vm_object` layers.
- **Allocation:** `mmap` creates a new `vm_map_entry`. Anonymous mappings use a `VM_OBJ_TYPE_DEFAULT` object.
- **Fault Handling:** Memory is lazily allocated via `vm_fault` when first accessed.

## Constraints
- Address and length must be page-aligned for `munmap`.
- `brk` must not overlap with other mappings.
