# Zone Allocator (UMA) Specification

## Overview
Substrate uses a BSD-inspired Universal Memory Allocator (UMA) as the kernel's
fixed-size object allocator. UMA is the layer beneath `kmalloc()` for small
allocations and is also used directly by subsystems that want dedicated object
caches with constructors, destructors, or debugging policy.

The active architecture is:
- zone
- slab
- bucket
- per-CPU cache

## Core Data Model

### Zone
A zone represents one object class.

Each `uma_zone_t` records:
- object size (`uz_size`)
- real per-object size after alignment/redzones (`uz_rsize`)
- items per slab (`uz_ipers`)
- callback hooks (`ctor`, `dtor`, `init`, `fini`)
- flags such as `UMA_ZONE_MALLOC`, `UMA_ZONE_OFFPAGE`, `UMA_ZONE_REDZONE`
- three slab lists:
  - full
  - partial
  - completely free
- one per-CPU cache array sized from the detected CPU count

Zones are created from:
- a static bootstrap pool before `kmem_init()`
- `kzalloc()` after `uma_enable_dynamic_alloc()`

### Slab
A slab is the backing storage for a zone.

Current slab behavior:
- backing memory comes from PMM pages
- small/medium objects prefer on-page slab metadata
- large objects use `UMA_ZONE_OFFPAGE`, with slab headers allocated separately
- on-page slabs rotate a per-slab starting offset when slack permits, so
  successive slabs color the first object at different cache-set offsets
- free objects are tracked by an index freelist (`us_freelist`) rather than
  embedding next-pointers in client memory
- slabs are hashed by backing page address for reverse lookup during free

### Bucket / Magazine Layer
Substrate's fast path uses small per-CPU buckets:
- `uc_allocbucket`
- `uc_freebucket`
- a shared empty-bucket depot
- a shared full-bucket depot keyed by owning zone

These buckets act as magazines for low-contention allocation/free traffic.
When a bucket is empty or full, the allocator first consults the shared depot
before falling back to slab lists.

### Per-CPU Cache
Each zone owns a per-CPU `uma_cache_t` array.

The intent is:
- reduce lock contention on hot allocation paths
- preserve locality for repeatedly allocated small objects
- keep the slow path isolated in slab management

## Allocation Flow
1. `uma_zalloc()` checks the current CPU's bucket cache.
2. If the fast-path bucket has an item, allocation completes immediately.
3. Otherwise UMA allocates from a partial slab, then a free slab, then a new slab.
4. `uma_slab_alloc_item()` removes one object index from the slab freelist.
5. Optional redzone/poison logic is applied according to zone flags.

Free flow is the inverse:
1. `uma_zfree()` tries to return the item through the current CPU cache.
2. Slow-path frees resolve the owning slab through the page hash.
3. The object index is returned to the slab freelist.
4. Fully free slabs are either cached on the zone free list or returned to PMM,
   depending on zone policy.

## Bootstrap and Dynamic Transition
UMA startup occurs before `kmem_init()`, so it cannot initially depend on
`kmalloc()`.

Current transition:
- `uma_startup()` initializes global allocator state and bootstrap pools
- early zones come from the static bootstrap zone array
- `kmem_init()` creates the `kmem-*` malloc zones
- `uma_enable_dynamic_alloc()` then allows later zone metadata to be allocated
  dynamically through `kzalloc()`

This avoids recursive allocator bring-up while still letting UMA scale after the
general kernel allocator is online.

## Debugging / Safety Features
Supported policy flags include:
- redzones
- poison/trash filling, with free-pattern validation on reallocation to catch
  use-after-free scribbles
- leak tracking
- off-page slab headers
- malloc-zone tagging

The implementation also keeps a global slab hash so frees can validate that an
object belongs to the expected zone before returning it.

Zones may also register an optional reclaim callback. `uma_reclaim()` first
drains bucket/slab caches for each zone, then invokes that zone callback so
subsystem-specific caches can release additional memory under pressure.

## Constraints
- bootstrap zone metadata is finite until dynamic allocation is enabled
- bucket arrays are statically bounded by `MAX_CPUS`
- off-page slab headers still recurse through `kzalloc()`, so the bootstrap
  transition order matters
- reclamation exists for fully free slabs, but this is not yet a full
  general-purpose memory-pressure policy interface
