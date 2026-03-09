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
- a map-local reader/writer lock for structural updates and read-side traversal

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
- `vm_map_lock()` / `vm_map_unlock()` provide writer exclusion and
  `vm_map_lock_read()` / `vm_map_unlock_read()` provide read-side traversal protection
- adjacent entries with matching object identity, contiguous object offset, and identical mapping attributes are coalesced automatically

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
- anonymous default-object zero-fill handled directly in `vm_fault()` rather than by a standalone default pager instance

Current swap pager / backing-store contract:

- `vm_swapon(node)` accepts a writable `fs_node_t` that is either `FS_FILE` or `FS_BLOCKDEVICE`
- swap capacity is derived from `node->length / PAGE_SIZE`
- swap space allocation is tracked by a global bitmap
- each swap pager keeps a per-`pindex` swap-block table
- `vm_swapoff()` disables the active backend only when no swap blocks remain allocated

Current device pager note:

- `VM_OBJ_TYPE_DEVICE` is recognized by the generic pager allocator
- a device pager instance records a physical base, mapping length, and protection mask
- `/dev/mem` and `/dev/fb0` install `VM_OBJ_TYPE_DEVICE` mappings backed by that pager instead of eagerly entering PTEs
- `vm_fault()` resolves a device-backed fault by translating `pindex` to the requested physical page and inserting it directly into the target `pmap`
- device-backed mappings do not populate a resident `vm_page_t` in the object; the pager is authoritative for the physical mapping target

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
6. either zero-fill the page, fetch it from the pager, or resolve a direct device physical page through the device pager
7. map the result through `pmap_enter`

Implemented behaviors:

- anonymous zero-fill on demand
- file-backed pager fetch through the vnode pager path
- simple prefault of the next sequential page when the pager reports it present
- copy-on-write duplication on write fault

Current design note:

- the machine-independent VM path supports shadow objects and pager-backed faults
- `vm_map_fork()` now constructs distinct parent and child shadow objects for `VM_INHERIT_COPY` entries, then downgrades the parent PTEs to force later write faults through the shadow chain

## User Memory Syscall Contract

Current `sys_mmap()` behavior in `sys/vm/vm_syscalls.c`:

- lengths are rounded up to page size before insertion
- `MAP_FIXED` requires a page-aligned address and removes any existing mapping in the target range first
- anonymous mappings are lazy: `mmap()` installs the `vm_map` entry and first access allocates a zero-filled page through `vm_fault()`
- file-backed mappings are lazy and pager-backed
- device-backed mappings installed by kernel character devices are also lazy and fault-driven

Current file-backed mapping contract:

- `MAP_SHARED` creates a vnode-backed `vm_object` with a vnode pager attached directly to the entry
- `MAP_PRIVATE` creates a top-level shadow object over a vnode-backed pager object
- file-backed mappings do not prepopulate resident pages during `mmap`; faults populate from the pager path
- private file mappings do not write back through `msync()` because the top-level private shadow object has no pager
- shared file mappings write back dirty resident pages through the vnode pager on `msync()`
- a fork of a `MAP_SHARED` file mapping shares the same backing `vm_object`, so resident pages remain physically shared across parent and child
