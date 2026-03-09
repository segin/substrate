# x86_64 Large Page Promotion and Demotion

## Scope

This document defines the intended large-page policy for the x86_64 PMAP.

Supported hardware page sizes on AMD64 are:

- 2MB pages through `PD` entries with `PS=1`
- 1GB pages through `PDPT` entries with `PS=1` when CPUID reports support

## Goals

Large pages are meant to reduce:

- TLB pressure for large contiguous mappings
- page-table memory overhead
- page-walk depth on hot kernel and anonymous regions

## Entry points

Planned PMAP entry points are:

- `pmap_enter_2mb(pmap, va, pa, prot, flags)`
- `pmap_remove_2mb(pmap, va)`
- `pmap_enter_1gb(pmap, va, pa, prot, flags)`
- `pmap_remove_1gb(pmap, va)`

## Promotion policy

Promotion is a best-effort optimization, not a correctness requirement.

Promotion to 2MB is valid only when:

1. virtual base is 2MB-aligned
2. physical base is 2MB-aligned
3. the covered `512` 4KB mappings are contiguous
4. the covered mappings have identical protection and caching attributes
5. the mapping is not blocked by wiring, device, or mixed-ownership semantics

Promotion to 1GB is valid only when:

1. virtual base is 1GB-aligned
2. physical base is 1GB-aligned
3. the covered `512` PD entries form a fully compatible contiguous span
4. CPUID reports 1GB-page support

## Demotion policy

Demotion is mandatory whenever a subrange operation would otherwise partially
edit a large mapping.

Demotion triggers include:

- partial unmap
- partial protect
- COW write fault within a large mapping
- mixed accessed/dirty accounting that requires 4KB granularity

Required demotion behavior:

- 2MB page -> allocate one PT page and materialize `512` 4KB PTEs
- 1GB page -> allocate one PD page and materialize `512` 2MB PDEs, with further
  demotion to PTs if a later operation needs 4KB granularity

## Accounting rules

Large-page mappings must still integrate cleanly with VM accounting:

- resident and mapped counts track the covered 4KB-page equivalents
- reverse mappings and `vm_page_t` holds remain consistent with the represented
  physical coverage
- teardown removes PMAP holds without claiming ownership of the underlying
  physical data pages

## Kernel-use policy

Expected first-use targets are:

- stable kernel text/data windows
- large anonymous regions
- direct-map ranges where the physical layout is sufficiently contiguous

User mappings may use large pages opportunistically, but correctness must never
depend on promotion succeeding.
