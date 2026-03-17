# PMAP Layer Specification (i386 & x86_64)

## Overview
The `pmap` layer is the machine-dependent part of the virtual memory system. It manages the hardware page tables and TLB flushes for a specific architecture.

## Design
- **Address Space:** Each `pmap` structure represents a unique set of hardware page tables.
- **i386 Implementation Details:**
  - Page directory + page tables.
  - Recursive self-map at PDE 1023.
  - `V_PD` at `0xFFFFF000` and `V_PT(n)` at `0xFFC00000 + n * 4096`.
  - Per-process user PDEs in slots `0..767`.
  - Shared kernel PDEs in slots `768..1022`.
  - Bootstrap direct map of physical `0..1004MB` in the higher half, with PDE 1019 reserved for LAPIC MMIO.
  - When PSE is available, the first higher-half 4MB window (`0xC0000000..0xC03FFFFF`) is installed as a single large PDE.
- **Recursive Paging:** PTs are accessed via a self-referential entry in the top-level directory (index 1023 for i386, 511 for x86_64).
- **Invalidation:** Hardware TLB is flushed via `invlpg` or CR3 reloads.
- **i386 TLB Strategy:**
  - Single-page updates use `invlpg`.
  - Bulk local flushes use CR3 reload.
  - SMP invalidation uses LAPIC IPIs to other CPUs plus an acknowledgement barrier.
  - Kernel mappings use PGE/global bits when supported.
  - Global flushes use the CR4.PGE toggle path.
- **Kernel PDE propagation:** i386 pmaps copy kernel PDEs at creation time and `pmap_growkernel()` propagates newly allocated kernel PDEs into already-existing pmaps when kernel mappings expand into a previously unused PDE.
- **Per-pmap accounting:** i386 pmaps track resident, wired, and mapped page counts plus fault/COW statistics alongside the underlying page-directory state.
- **Per-page mapping holds:** i386 PMAP insert/remove/fork paths maintain both reverse mappings (`pv_list`) and one `vm_page.ref_count` hold per live mapping. `pmap_destroy()` tears down those mapping holds but does not claim ownership of the underlying data pages.
- **Large-page support (i386):** i386 supports 4 MB PSE mappings through `pmap_enter_large()`. Partial `pmap_remove()` and `pmap_protect()` operations demote a 4 MB PDE into a normal 1024-entry 4 KB page table before applying the subrange change.
- **Large-page tracking:** i386 large-page mappings are tracked as 1024 constituent 4 KB mappings for PV/refcount purposes so teardown and later demotion preserve reverse-mapping integrity.

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

### `int pmap_enter_large(pmap_t pmap, uint64_t va, uint64_t pa, uint64_t prot, uint32_t flags)`
Creates a 4 MB i386 PSE mapping when both `va` and `pa` are 4 MB aligned and the target `pmap` is active.

## Constraints
- i386 supports 4 MB PSE pages only. Promotion of 1024 4 KB mappings back into a 4 MB mapping is still deferred.
- x86_64 large-page support (2 MB / 1 GB) remains future work.
- Recursive paging addresses are hardcoded based on the selected index.
