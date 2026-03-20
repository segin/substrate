# Buffer Cache (bio) Specification

## Overview
The buffer cache provides a BSD-style block I/O layer that sits between filesystems and device drivers. It caches disk blocks in kernel memory, reduces redundant I/O, and supports delayed writes for batched throughput.

Implementation resides in `sys/vfs/bio.c` with data structures in `sys/vfs/buf.h`.

## Data Structures

### `struct buf`
| Field | Type | Purpose |
|-------|------|---------|
| `b_flags` | `uint32_t` | Status flags (see below) |
| `b_data` | `void *` | Pointer to buffer data (kmalloc-backed) |
| `b_bcount` | `size_t` | Buffer size in bytes |
| `b_blkno` | `int64_t` | Physical block number |
| `b_lblkno` | `int64_t` | Logical block number |
| `b_vp` | `struct vnode *` | Associated vnode |
| `b_rcred` | `struct ucred *` | Read credentials |
| `b_wcred` | `struct ucred *` | Write credentials |
| `b_resid` | `size_t` | Residual byte count after I/O |
| `b_iodone` | `biodone_t` | Callback invoked on I/O completion |
| `b_error` | `int` | Error code (valid when `B_ERROR` set) |
| `b_qindex` | `int` | Current queue index (`BQ_*`, or -1) |
| `b_hash` | `LIST_ENTRY` | Hash table linkage |
| `b_freelist` | `TAILQ_ENTRY` | Queue linkage |

### Buffer Flags
| Flag | Value | Meaning |
|------|-------|---------|
| `B_BUSY` | 0x01 | Buffer is locked (in use) |
| `B_DONE` | 0x02 | I/O complete |
| `B_ERROR` | 0x04 | I/O error occurred |
| `B_DELWRI` | 0x08 | Delayed write pending |
| `B_PHYS` | 0x10 | Physical (DMA) I/O |
| `B_READ` | 0x20 | Read operation |
| `B_WRITE` | 0x40 | Write operation |
| `B_ASYNC` | 0x80 | Asynchronous I/O |
| `B_INVAL` | 0x100 | Invalidate after I/O |
| `B_NOCACHE` | 0x200 | Do not cache |
| `B_CACHE` | 0x400 | Valid cache hit |

## Buffer Queues

Four TAILQ queues manage buffer lifecycle:

| Queue | Constant | Purpose |
|-------|----------|---------|
| LOCKED | `BQ_LOCKED` (0) | Buffers currently in use |
| CLEAN | `BQ_CLEAN` (1) | Cached, unmodified, available for reuse |
| DIRTY | `BQ_DIRTY` (2) | Modified, pending write (`B_DELWRI` set) |
| EMPTY | `BQ_EMPTY` (3) | Free buffers available for allocation |

Queue transitions:
- `getblk()` → LOCKED
- `brelse()` → DIRTY (if `B_DELWRI`), CLEAN (if clean), or EMPTY (if invalidated)
- `bufsync()` → LOCKED (during write), then CLEAN or EMPTY after `bwrite()`

## Hash Table Lookup

A 256-bucket hash table provides O(1) lookup by `(vnode, blkno)`:
```c
static inline uint32_t bio_hash(struct vnode *vp, int64_t blkno) {
    return (((uintptr_t)vp >> 4) ^ (uint64_t)blkno) % BIO_HASH_SIZE;
}
```

Lookups are performed via the internal `bio_lookup_locked()` and the public `incore()` API.

## API

### `struct buf *getblk(struct vnode *vp, int64_t blkno, size_t size, int slpflag, int slptimeo)`
Allocates or retrieves a locked buffer for the given vnode/block. On cache hit, returns existing buffer; on miss, allocates new or reuses from EMPTY/CLEAN queues. Maximum buffer count is `BIO_NBUF_MAX` (512).

### `int bread(struct vnode *vp, int64_t blkno, size_t size, struct ucred *cred, struct buf **bpp)`
Synchronous block read. Calls `getblk()`, then invokes `VOP_STRATEGY()` on cache miss and waits via `biowait()`.

### `int breada(struct vnode *vp, int64_t blkno, size_t size, int64_t rablkno, size_t rabsize, struct ucred *cred, struct buf **bpp)`
Read with read-ahead. Performs primary `bread()` synchronously, then issues an asynchronous read for the specified read-ahead block.

### `int bwrite(struct buf *bp)`
Synchronous write. Clears `B_DELWRI`, invokes `VOP_STRATEGY()`, and blocks until completion (unless `B_ASYNC` set).

### `int bawrite(struct buf *bp)`
Asynchronous write. Sets `B_ASYNC` and calls `bwrite()`.

### `void bdwrite(struct buf *bp)`
Delayed write. Sets `B_DELWRI` flag without initiating I/O; `brelse()` moves the buffer to BQ_DIRTY for later flush by the syncer daemon or `sync()`.

### `void brelse(struct buf *bp)`
Releases a buffer. Clears `B_BUSY`, reassigns to appropriate queue (DIRTY/CLEAN/EMPTY), and wakes all sleeping waiters.

### `int biowait(struct buf *bp)`
Blocks on sleep queue until `B_DONE` is set. Returns error code if `B_ERROR` set.

### `void biodone(struct buf *bp)`
Signals I/O completion. Sets `B_DONE`, invokes optional `b_iodone` callback, wakes waiters.

### `struct buf *incore(struct vnode *vp, int64_t blkno)`
Non-blocking hash table lookup. Returns buffer if cached, NULL otherwise. Does not lock or modify queues.

### `int binval_vnode(struct vnode *vp, int save)`
Invalidates all cached buffers for a vnode. If `save=1`, dirty buffers are flushed first. Used during unmount or vnode reclaim.

### `int bufsync(int freq)`
Flushes all dirty buffers synchronously. Drains BQ_DIRTY queue by calling `bwrite()` on each buffer.

## Syncer Daemon

A kernel thread (`syncer_daemon`) runs periodically to flush dirty buffers:
- **Period:** 30 seconds (timer-based via `sched_sleep_until()`)
- **Action:** Calls `bufsync()` to drain BQ_DIRTY
- **Started:** By `bio_init()` at boot via `kthread_create()`
- **Sync integration:** `sys_sync()` also calls `bufsync(0)` for immediate flush

## Locking

- **`bio_lock` (spinlock):** Protects all hash table and queue operations, flag modifications, and buffer count tracking.
- **Sleep queues:** Used for blocking on `B_BUSY` (in `getblk()`) and `B_DONE` (in `biowait()`). Wakeup via `sleepq_wake_all()` in `brelse()` and `biodone()`.

## Buffer Lifecycle

```
getblk(vp, blk) → [hash miss] → allocate + BQ_LOCKED
bread()          → VOP_STRATEGY() → biodone() → data ready
modify in memory
bdwrite()        → B_DELWRI → brelse() → BQ_DIRTY
syncer/sync()    → bufsync() → bwrite() → BQ_CLEAN
getblk() reuse   → [hash hit] → B_CACHE, return locked
```
