# x86_64 Recursive Paging

## Purpose

Recursive paging gives the active x86_64 address space a stable virtual window
through which it can inspect and edit its own page tables without temporary
mapping tricks.

Substrate reserves `PML4` slot `510` for this purpose.

## Layout

With slot `510` self-referencing the active `PML4`, the recursive windows are:

- `V_PML4`: the current PML4 page
- `V_PDPT(pml4i)`: the PDPT page under a selected PML4 entry
- `V_PD_INDEX(pml4i, pdpti, k)`: the PD page under a selected PDPT entry
- `V_PT_INDEX(pml4i, pdpti, pdi, l)`: the PT page under a selected PD entry

The active headers define these windows from the slot-510 base:

- `PG_V_PT     = 0xFFFFFF0000000000`
- `PG_V_PD     = 0xFFFFFF8000000000`
- `PG_V_PDPT   = 0xFFFFFFC000000000`
- `PG_V_PML4   = 0xFFFFFFE000000000`

## Index extraction

Virtual-address decoding uses 9-bit indices per level:

- `PML4_INDEX(va)` = bits `47:39`
- `PDPT_INDEX(va)` = bits `38:30`
- `PD_INDEX(va)`   = bits `29:21`
- `PT_INDEX(va)`   = bits `20:12`

Each table has `512` entries, and each entry is 8 bytes wide.

## Usage rules

The recursive window is only valid for the currently active pmap. That leads to
two operating modes:

- active-pmap fast path: use recursive macros directly
- inactive-pmap slow path: access page-table memory through its kernel virtual
  alias or via an explicit temporary mapping strategy

This is the same practical distinction already visible in the current x86_64
PMAP stubs.

## Invariants

Recursive paging on x86_64 must preserve these invariants:

1. `PML4[510]` always points at the active `PML4` physical page
2. the recursive entry itself is present and writable
3. kernel code never treats recursive-window addresses as stable across a `CR3`
   switch to another pmap
4. edits done through the window are followed by the correct TLB invalidation

## Interaction with large pages

Recursive access still works when a `PDPT` or `PD` entry maps a large page, but
the walk stops earlier:

- 1GB page: terminal mapping at the `PDPT` level
- 2MB page: terminal mapping at the `PD` level

Callers must check `PTE_PS` before assuming that a lower-level table exists.
