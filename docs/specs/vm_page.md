# VM Page Specification

## Overview
A `vm_page` tracks the state of a single physical page of memory. It serves as the bridge between the machine-independent VM layer and the physical memory manager.

## Design
- **Core fields:**
    - `phys_addr`: canonical physical address of the tracked frame.
    - `flags`: state and writeback bits for queueing, validity, and swap/writeback lifecycle.
    - `wire_count`: pin count; pages with a non-zero wire count cannot be reclaimed.
    - `ref_count`: page hold count. One hold comes from the allocator/object lifecycle, and each live PMAP mapping adds one additional hold.
    - `order`: buddy allocator order for free-list membership and coalescing.
    - `object` + `pindex`: owning VM object and per-object page index.
    - `pv_list`: reverse-mapping chain of `(pmap, va)` users of the page.
- **States:**
    - `PG_FREE`: On the PMM buddy free lists and available for allocation.
    - `PG_ACTIVE`: Recently used and protected from immediate reclamation.
    - `PG_INACTIVE`: A reclaim candidate once it ages out of active use.
    - `PG_BUSY`: Undergoing I/O or a transient mapping transition.
    - `PG_VALID`: Contains initialized data for its object or backing store.
    - `PG_DIRTY`: Needs writeback before final reclamation.
    - `PG_ZERO`: Known-zero page that can satisfy zero-fill demand cheaply.
    - `PG_SWAPPED`: Contents have a swap-backed copy.
    - `PG_WRITEBACK`: Currently being written to backing store.
- **Queues:** Resident pages are managed in doubly-linked lists.
    - `active_queue`: recently used pages
    - `inactive_queue`: reclaim candidates
    - `wired_queue`: pinned pages that cannot be paged out
    - `laundry_queue`: dirty pages waiting for writeback completion
- **Identity:** Each page is identified by its `phys_addr` and its relationship to a `vm_object` + `pindex`.
- **Reverse mappings:** `pv_entry` chains associate a physical page with each `(pmap, va)` mapping so the PMAP layer can answer reference/dirty questions and resolve COW faults.
- **Mapping holds:** PMAP insert/remove paths keep `ref_count` aligned with `pv_list`, so replacing or removing a mapping drops exactly one mapping hold without transferring ownership of the underlying page.
- **Bootstrap path:** PMM allocates the `vm_page_t[]` database during early boot, initializes each slot with canaries and `phys_addr`, then seeds the buddy free lists from accepted usable physical ranges.

## API
### `void vm_page_init(void)`
Initializes the global page queues.

### `vm_page_t *vm_page_alloc(struct vm_object *object, uint64_t pindex, int req)`
Allocates a page from the free list and associates it with an object.

### `void vm_page_free(vm_page_t *m)`
Returns a page to the free list and disassociates it from any object.

### `uintptr_t vm_page_to_phys(const vm_page_t *m)`
Returns the physical address tracked by the page descriptor.

### `void vm_page_insert(vm_page_t *page, struct vm_object *object, uint64_t pindex)`
Links a page into its owning VM object index.

### `void vm_page_remove(vm_page_t *page)`
Removes a page from its owning VM object index and clears ownership metadata.

### `void pv_insert(vm_page_t *page, struct pmap *pmap, uintptr_t va)`
Adds one reverse-mapping backlink for the given `(pmap, va)` mapping.

### `void pv_remove(vm_page_t *page, struct pmap *pmap, uintptr_t va)`
Removes one reverse-mapping backlink.

### `void pv_remove_all(vm_page_t *page)`
Clears every reverse-mapping backlink before final reclamation.

## Queue State Machine
- Freshly allocated pages enter the VM layer as resident but unqueued and `PG_BUSY`.
- `vm_page_activate()` moves a page to `active_queue` and assigns an initial age.
- `vm_page_deactivate()` moves a page from active use to `inactive_queue`.
- `vm_page_wire()` removes a page from active/inactive circulation and places it on `wired_queue`.
- `vm_page_unwire()` returns a once-pinned page to `active_queue`.
- `vm_page_launder()` moves a dirty inactive page to `laundry_queue`, performs writeback, then returns it to `inactive_queue` if successful.
- `vm_page_try_to_free()` reclaims a clean inactive page back into the PMM free lists.

Queue invariants:
- A page may appear on at most one resident queue at a time.
- `prev`/`next` links must remain internally consistent for every queue node.
- Accounting for `free_count + active + inactive + wired + laundry` must remain stable across queue moves when the set of resident pages is unchanged.

## Page Daemon
The `pagedaemon` kernel process is started by `vm_page_late_init()`. It sleeps on the `vm_pages_needed` wakeup channel with a one-second timeout and then calls `vm_pageout()` whenever explicit pressure or threshold-based pressure exists. When the selected page-aging policy is `VM_PAGE_POLICY_LRU_APPROX`, the daemon also runs `vm_page_age_scan()` on wakeups to keep age counters current.

`vm_pageout()` currently runs in phases:
1. reclaim UMA-backed kernel allocations
2. free clean inactive pages already available for reclaim
3. launder dirty inactive pages and retry freeing them
4. only if still short, scan the active queue and move cold pages to inactive before repeating reclaim

Active-page scanning is policy-driven:
- `VM_PAGE_POLICY_CLOCK`: unreferenced active pages move inactive immediately on the next scan.
- `VM_PAGE_POLICY_LRU_APPROX`: unreferenced active pages age down in place and only move inactive when their age reaches zero.

The wakeup path is:
- PMM allocation paths call `vm_page_wakeup_daemon()` once free memory dips near reserved levels.
- The page daemon also self-triggers when `vm_page_should_pageout()` observes free memory below `free_target` or excessive dirty inactive pages.

## Tuning Parameters
- `free_reserved`: emergency reserve for kernel allocations
- `free_min`: panic threshold; system is critically low below this level
- `free_target`: the steady-state free-page target; pageout should run below this level
- `inactive_target`: desired amount of reclaimable inactive memory kept on hand

Thresholds start from conservative defaults and are raised proportionally to total RAM during `vm_page_late_init()`.

## Constraints
- OOM handling now selects a non-kernel, non-init process victim and delivers `SIGKILL`, but reclaim still completes asynchronously when that victim exits.
- Page-aging policy selection is currently an internal kernel knob rather than a sysctl or tunable exported to userspace.
