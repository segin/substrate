# VM Subsystem Contract

## Scope

This document records the current machine-independent VM contract implemented in `sys/vm/`.
It describes the actual design in tree today rather than an idealized future VM.

## Hierarchy

The current hierarchy is:

- `vm_map`: one virtual address space description per process
- `vm_map_entry`: one contiguous virtual region within that address space
- `vm_object`: backing object for the region
- `vm_page`: resident physical page owned by a backing object

`vm_map` holds:

- a sorted doubly-linked entry list for sequential traversal
- a splay tree of entries for lookup locality
- a red-black tree of free holes for first-fit free-space search
- the associated machine-dependent `pmap`

## `vm_map` Contract

Implemented entry-management API:

- `vm_map_create(pmap, min, max)`
- `vm_map_destroy(map)`
- `vm_map_insert(map, object, offset, start, end, prot, max_prot, inheritance)`
- `vm_map_remove(map, start, end)`
- `vm_map_lookup(map, va)`
- `vm_map_protect(map, start, end, prot)`
- `vm_map_inherit(map, start, end, inheritance)`
- `vm_map_wire(map, start, end)`
- `vm_map_unwire(map, start, end)`
- `vm_map_fork(src_map, dst_pmap)`

Entry semantics:

- `start` / `end` define a half-open virtual range `[start, end)`
- `offset` is the byte offset into the backing `vm_object`
- `protection` is the current protection mask
- `max_protection` is the upper bound accepted by `vm_map_protect`
- `inheritance` controls fork behavior (`SHARE`, `COPY`, `NONE`, `ZERO`)
- `wire_count` tracks explicit wiring state

Current design note:

- entry lookup uses a splay tree, not a red-black tree
- hole lookup uses a red-black tree keyed by free ranges
- map-level reader/writer locking is not implemented yet
- automatic coalescing of adjacent entries is not implemented yet

## `vm_object` Contract

A `vm_object` represents a backing store abstraction for anonymous, vnode-backed, device, physical, and swap-style memory.

Implemented fields/behavior:

- object type and size
- object `ref_count`
- resident page list keyed by per-page `pindex` through linear lookup
- optional `pager`
- optional `shadow` object and `shadow_offset`

Reference ownership rule:

- `vm_object` references are owned explicitly by maps, shadow chains, pagers, and callers
- `vm_map_insert()` consumes the caller-owned reference instead of taking an extra one
- callers that want to retain a handle after insertion must call `vm_object_reference()` before inserting

Implemented API:

- `vm_object_allocate(type, size)`
- `vm_object_reference(object)`
- `vm_object_deallocate(object)`
- `vm_object_lookup_page(object, pindex)`
- `vm_object_add_page(object, page)`
- `vm_object_remove_page(object, page)`
- `vm_object_shadow(source)`
- `vm_object_collapse(object)`

Current collapse rule:

- collapse is allowed only when the object has a shadow
- `shadow_offset` must be zero
- the immediate shadow must be singly referenced
- the object must already hold a full resident page set for its declared size
- collapse drops the no-longer-needed parent shadow reference and clears the chain

This is intentionally conservative. It removes backing dependencies only when the child object is already self-contained.

## Pager Contract

The pager layer is intentionally small and synchronous.

Implemented generic API:

- `vm_pager_allocate(type, handle, size, prot, offset)`
- `vm_pager_deallocate(pager)`
- `vm_pager_get_pages(pager, pages, count, sync)`
- `vm_pager_put_pages(pager, pages, count, sync)`
- `vm_pager_has_page(pager, pindex)`

Implemented pager families:

- swap pager
- vnode pager
- device pager

Current vnode pager contract:

- `pager->priv` is an `fs_node_t *`
- `getpage` reads one page through VFS `read`
- short file reads are zero-filled to page size
- `putpage` writes one page through VFS `write`
- `haspage` is permissive and lets `getpage` resolve EOF/short-read behavior

## Fault Handling Contract

`vm_fault(map, va, fault_type)` currently performs:

1. locate the covering `vm_map_entry`
2. check requested protection against entry protection
3. walk the shadow chain looking for a resident page
4. resolve copy-on-write when a write targets a shared backing page
5. allocate a new page if none is resident
6. either zero-fill the page or fetch it from the pager
7. map the result through `pmap_enter`

Implemented behaviors:

- anonymous zero-fill on demand
- file-backed pager fetch through the vnode pager path
- simple prefault of the next sequential page when the pager reports it present
- copy-on-write duplication on write fault

Current design note:

- the machine-independent VM path supports shadow objects and pager-backed faults
- `vm_map_fork()` currently implements COW by sharing the backing object and downgrading parent protections rather than constructing a new shadow object per entry during fork
