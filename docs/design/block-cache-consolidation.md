# Block-Level Buffer Cache Consolidation

## 1. Problem Statement

Substrate has **two** disk caches:

1. `sys/vfs/bio.c` — a BSD-style buffer cache (hash index, `B_BUSY` +
   sleepq serialization, `B_DELWRI` delayed write, syncer kthread). It is
   correct but is invoked **explicitly by the filesystem** (`ext2` calls
   `bio_dev_get`), which couples cache policy into filesystem code.
2. `sys/drivers/storage/blkdev.c` — an ad-hoc 1024-entry sector array
   (`bcache`) below the filesystem. It is transparent to drivers (the
   desired layer) but is **rudimentary and largely bypassed**: bulk reads
   skip it (and invalidate the range), lookup is an O(N) linear scan, and
   it issues blocking device I/O **while holding a spinlock** — a deadlock
   hazard for AHCI/IDE/NVMe now that kernel preemption is enabled.

The result is double-caching, divergent locking discipline, and a
filesystem that "knows" about caching.

## 2. Goal

A **single, transparent, block-level read cache** living at the block-
device abstraction. It must work for every driver that registers a
`blkdev_t` with no driver changes, and the filesystem layer must hold no
caching logic at all. The `bio.c` buffer cache becomes the one engine;
the `blkdev` ad-hoc array is retired; `ext2` is stripped of cache calls.

## 3. User Stories

- **US-1 (FS author):** As a filesystem author, I want block caching to
  happen below my code, so that I never embed cache calls and every
  filesystem benefits automatically.
- **US-2 (Driver author):** As a storage-driver author, I want caching to
  require zero driver changes, so that any device registered as a
  `blkdev_t` is cached the instant it appears.
- **US-3 (End user):** As a user, I want repeated reads of the same disk
  data served from RAM, so that I/O-bound work (builds, `grep -r`,
  directory walks) runs noticeably faster.
- **US-4 (Maintainer):** As a kernel maintainer, I want exactly one cache
  implementation, so that there is no double-caching and no second,
  divergent locking model to keep correct.
- **US-5 (End user):** As a user, I want sequential / bulk reads to also
  benefit from the cache, so that large-file reads are not slower than
  before.
- **US-6 (Maintainer):** As a kernel maintainer, I want device I/O to
  never run while a spinlock is held, so that blocking drivers cannot
  deadlock or wedge preemption.

## 4. Requirements (INCOSE / EARS)

### Functional

- **BLK-1 — Single keyed cache (ubiquitous):**
  The block layer shall cache device sectors in one shared buffer cache
  keyed by the tuple (block device, sector number).

- **BLK-2 — Serve hits (event-driven):**
  When a sector read is requested and a valid cached copy of that sector
  exists, the block layer shall return the cached copy without issuing a
  device read.

- **BLK-3 — Populate on miss (event-driven):**
  When a sector read is requested and no valid cached copy exists, the
  block layer shall read the sector from the device, store it in the
  cache, and return it.

- **BLK-4 — Mixed multi-sector read (event-driven):**
  When a multi-sector read spans both cached and uncached sectors, the
  block layer shall serve the cached sectors from the cache and shall
  coalesce each maximal run of contiguous uncached sectors into a single
  device read.

- **BLK-5 — Write-through (event-driven):**
  When a sector write is requested, the block layer shall write the data
  through to the device and update (or insert) the corresponding cached
  copy within the same operation, preserving current write-through
  durability.

- **BLK-6 — No stale caching on failure (unwanted behavior):**
  If a device read or write reports failure, then the block layer shall
  not leave a cached copy of the affected sectors.

- **BLK-7 — Filesystem is cache-agnostic (ubiquitous):**
  The filesystem layer shall contain no device-block caching logic and
  shall obtain all device data through the block layer's read/write
  interface.

### Constraints / Non-Functional

- **BLK-8 — Lock-free device I/O (state-driven, unwanted):**
  While performing device I/O to satisfy a cache miss, the block layer
  shall not hold a spinlock; concurrency shall be serialized via a
  per-buffer busy flag with sleepq waiting.

- **BLK-9 — Sub-linear lookup (ubiquitous):**
  The block cache shall locate a cached sector in better-than-linear time
  with respect to the number of cached buffers (hash-indexed).

- **BLK-10 — One implementation (ubiquitous):**
  The system shall retain a single buffer-cache implementation
  (`sys/vfs/bio.c`); the ad-hoc `bcache` array in `blkdev.c` shall be
  removed.

- **BLK-11 — Single-filler serialization (unwanted behavior):**
  If two requests target the same uncached sector concurrently, then the
  block layer shall ensure only one device read populates the cache and
  the other observes the populated buffer.

- **BLK-12 — No bulk-read regression (ubiquitous):**
  The consolidation shall not increase the number of device I/O
  operations for a fully-uncached sequential read beyond the
  pre-consolidation direct-read path (i.e. misses are coalesced, not
  issued one sector at a time).

## 5. Verification

| Req | Method | Evidence |
|-----|--------|----------|
| BLK-1,2,3 | Test | repeated reads of the same blocks issue device I/O once (hit-counter instrumentation or device-read counter) |
| BLK-4,12 | Test | a cold sequential read of N sectors issues ⌈N/run⌉ coalesced reads, not N |
| BLK-5,6 | Test | write then read-back returns written data; injected short write leaves no stale cache |
| BLK-7 | Review | `ext2.c` contains no `bio_*` calls; `ext2_read_block`/`ext2_write_block` call only `fs->device->{read,write}` |
| BLK-8 | Review | no `dev->read/write` call sites under `bio_lock`/`bcache_lock`; buffer is `B_BUSY` with lock dropped during I/O |
| BLK-9,10 | Review | `bcache[]` array deleted; lookups go through `bio.c` hash |
| BLK-11 | Review/Test | concurrent same-sector readers serialize on `B_BUSY` |
| (all) | Regression | clean boot, ext2 mount, `rm -rf` of large/nested trees, normal zsh boot |

## 6. Design Outline

- `blkdev_do_read(dev, sector, count, buf)` and the bulk-aligned path in
  `blkdev_read_bytes` route through `bio_dev_get`/`incore`/`brelse`:
  - `incore()` peeks the cache without busying to find runs of misses;
  - one `dev->read()` per contiguous miss run (lock dropped, buffers
    busy), then each buffer is populated and flagged `B_CACHE`.
- `blkdev_do_write` / the bulk-aligned write path: `dev->write()` through,
  then update the cached buffer (`B_CACHE`), invalidate on failure.
- `ext2_read_block` → `fs->device->read(...)`; `ext2_write_block` →
  `fs->device->write(...)`. All `bio_dev_*` calls removed from `ext2.c`.
- Delete `bcache[]`, `bcache_lookup/evict/invalidate`, and the
  `blkdev_do_read/write` array logic; keep the function names as thin
  wrappers over the bio path.

## 7. Out of Scope

- Read-ahead / prefetch (separate follow-on; the cache makes it possible).
- Switching writes from write-through to write-back (durability change).
- Wiring additional filesystems beyond removing ext2's coupling.
