# PMAP Layer Specification (i386 & x86_64)

## Overview
The `pmap` layer is the machine-dependent part of the virtual memory system. It manages the hardware page tables and TLB flushes for a specific architecture.

## Design
- **Address Space:** Each `pmap` structure represents a unique set of hardware page tables.
- **Recursive Paging:** PTs are accessed via a self-referential entry in the top-level directory (index 1023 for i386, 511 for x86_64).
- **Invalidation:** Hardware TLB is flushed via `invlpg` or CR3 reloads.
- **Kernel PDE propagation:** i386 pmaps copy kernel PDEs at creation time and `pmap_growkernel()` propagates newly allocated kernel PDEs into already-existing pmaps when kernel mappings expand into a previously unused PDE.
- **Per-pmap accounting:** i386 pmaps track resident pages, mapped pages, and fault/COW statistics alongside the underlying page-directory state.

## API
### `void pmap_bootstrap(void)` (i386) / `void pmap_init(void)` (x86_64)
Initializes the kernel's initial address space and enables hardware paging.

### `int pmap_enter(pmap_t pmap, uint64_t va, uint64_t pa, uint64_t prot, uint32_t flags)`
Creates a hardware mapping from virtual to physical address. Allocates intermediate page tables if necessary.

### `void pmap_remove(pmap_t pmap, uint64_t va)`
Clears a hardware mapping and flushes the TLB.

### `uint64_t pmap_extract(pmap_t pmap, uint64_t va)`
Looks up the physical address currently mapped to a virtual address.

### `void pmap_activate(pmap_t pmap)`
Switches the CPU to the specified address space (reloads CR3).

### `void pmap_growkernel(uintptr_t va)`
Copies the kernel PDE covering `va` into every existing non-kernel pmap after the kernel allocates a new shared kernel page table.

## Constraints
- Does not currently support Large Pages (2MB/4MB/1GB).
- Recursive paging addresses are hardcoded based on the selected index.
