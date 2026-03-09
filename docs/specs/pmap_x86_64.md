# x86_64 PMAP Architecture

## Overview

The x86_64 PMAP layer is the planned four-level hardware mapping backend for
Substrate on AMD64. It follows the same high-level contract as the i386 PMAP:

- one `pmap` per address space
- recursive page-table access for the active address space
- `pmap_enter` / `pmap_remove` / `pmap_extract` / `pmap_protect`
- kernel mappings shared across processes
- COW integration through VM objects and `vm_page_t` reverse mappings

## Address-space model

The intended AMD64 address-space split is:

- lower canonical half: user mappings
- upper canonical half: kernel mappings
- kernel base: `0xFFFFFFFF80000000`

Current x86_64 PMAP headers and stubs already reserve this kernel base and
define four hardware levels:

- `PML4`
- `PDPT`
- `PD`
- `PT`

Each level contains `512` entries and uses 4KB tables.

## `struct pmap`

The x86_64 `pmap` contract mirrors the i386 bookkeeping model, with a 64-bit
top-level table:

- `pml4`: virtual address of the active top-level table
- `pml4_phys`: physical address loaded into `CR3`
- `ref_count`: shared/COW lifetime management
- `resident_count`: mapped resident pages
- `wired_count`: wired mappings
- `stats`: fault/COW/protection-change counters
- `lock`: SMP safety
- `asid`: reserved for future PCID support

## Bootstrap contract

`pmap_init()` is responsible for:

1. initializing the global pmap list and associated locks
2. establishing recursive mapping in the chosen PML4 slot
3. installing `kernel_pmap` from the bootstrap PML4
4. enabling `IA32_EFER.NXE` when supported
5. activating the kernel address space through `CR3`

## Core mapping contract

`pmap_enter()` on x86_64 is expected to:

1. walk `PML4 -> PDPT -> PD -> PT`
2. allocate missing intermediate tables on demand
3. derive final PTE flags from VM protection and PMAP flags
4. support user, write, global, and NX policy
5. invalidate the affected TLB entry when the target pmap is active

`pmap_remove()` clears the final mapping and invalidates the active translation.

`pmap_extract()` returns the physical backing address plus page offset.

`pmap_protect()` updates writable/user/execute semantics across a range.

## Shared kernel mappings

Like i386, the kernel address-space portion is intended to be authoritative in
`kernel_pmap` and copied or shared into newly created user pmaps. Kernel growth
must propagate into existing address spaces so newly allocated kernel page-table
roots remain visible everywhere.

## Feature growth path

The x86_64 PMAP is expected to absorb the following hardware features as the
implementation matures:

- `NX`
- `PGE`
- `PCID`
- `INVPCID`
- 2MB and 1GB large pages

Those features extend the same VM contract already used on i386 rather than
creating a separate VM design.
