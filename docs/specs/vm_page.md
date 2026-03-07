# VM Page Specification

## Overview
A `vm_page` tracks the state of a single physical page of memory. It serves as the bridge between the machine-independent VM layer and the physical memory manager.

## Design
- **States:**
    - `PG_FREE`: On the PMM buddy free lists and available for allocation.
    - `PG_ACTIVE`: Recently used and protected from immediate reclamation.
    - `PG_INACTIVE`: A reclaim candidate once it ages out of active use.
    - `PG_BUSY`: Undergoing I/O or a transient mapping transition.
    - `PG_DIRTY`: Needs writeback before final reclamation.
    - `PG_WRITEBACK`: Currently being written to backing store.
- **Queues:** Resident pages are managed in doubly-linked lists.
    - `active_queue`: recently used pages
    - `inactive_queue`: reclaim candidates
    - `wired_queue`: pinned pages that cannot be paged out
    - `laundry_queue`: dirty pages waiting for writeback completion
- **Identity:** Each page is identified by its `phys_addr` and its relationship to a `vm_object` + `pindex`.
- **Reverse mappings:** `pv_entry` chains associate a physical page with each `(pmap, va)` mapping so the PMAP layer can answer reference/dirty questions and resolve COW faults.

## API
### `void vm_page_init(void)`
Initializes the global page queues.

### `vm_page_t *vm_page_alloc(struct vm_object *object, uint64_t pindex, int req)`
Allocates a page from the free list and associates it with an object.

### `void vm_page_free(vm_page_t *m)`
Returns a page to the free list and disassociates it from any object.

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
The `pagedaemon` kernel process is started by `vm_page_late_init()`. It sleeps on the `vm_pages_needed` wakeup channel with a one-second timeout, runs `vm_page_age_scan()` opportunistically, and then calls `vm_pageout()` whenever explicit pressure or threshold-based pressure exists.

`vm_pageout()` currently runs in phases:
1. reclaim UMA-backed kernel allocations
2. free clean inactive pages already available for reclaim
3. launder dirty inactive pages and retry freeing them
4. only if still short, scan the active queue and move cold pages to inactive before repeating reclaim

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
- OOM handling is still a stub; the kernel reports critical low-memory instead of selecting a victim process.
- OOM handling is still a stub; there is no victim-selection policy yet.
