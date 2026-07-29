#include <vfs/buf.h>
#include <vfs/vnode.h>
#include <sys/lock.h>
#include <sys/errno.h>
#include <sys/kthread.h>
#include <kern/sched.h>
#include <kern/sleepq.h>
#include <kern/time.h>
#include <vm/vm_kmem.h>
#include <drivers/console/console.h>
#include <string.h>

#ifndef HOST_TEST
#include <arch/i386/pmm.h>
#else
/* Host harness supplies a stub. */
uint32_t pmm_get_free_memory(void);
#endif

/*
 * Buffer cache.  Traditional UNIX-style: a single hash + LRU pool that
 * grows dynamically into unused RAM and is reclaimed under pressure.
 *
 * Sizing policy:
 *   - Always allow growth up to BIO_NBUF_FLOOR buffers regardless of free
 *     RAM (so the cache works even when memory is tight).
 *   - Above the floor, only allocate a new buffer if pmm_get_free_memory()
 *     leaves at least BIO_RESERVE_BYTES available after the allocation.
 *   - Hard ceiling of BIO_NBUF_CEILING buffers to bound metadata overhead
 *     even on giant systems.
 *
 * Reclaim path: bio_reclaim(target_bytes) drains BQ_EMPTY, then BQ_CLEAN
 * (LRU first), freeing buf metadata and data pages until the target is
 * met.  Callers (vm pressure handlers, unmount, etc.) drive the policy.
 */
#define BIO_HASH_SIZE       8192
#define BIO_NBUF_FLOOR      64
#define BIO_NBUF_HARD_CAP   262144   /* absolute cap: bound metadata on big-RAM boxes */
#define BIO_RESERVE_BYTES   (16U * 1024U * 1024U)

/*
 * Runtime cache ceiling, sized to a fraction of total RAM at bio_init().
 * The old fixed 8192-buffer ceiling was a 4 MiB cache (8192 * 512 B) — far
 * too small for build/grep/link workloads, so hot data was evicted almost
 * immediately and nearly every demand-page fault fell through to a
 * synchronous device read.  bio_reclaim() is not yet wired to the VM
 * pressure path, so the cache never shrinks once grown; keep the ceiling at
 * 1/8 of RAM so the other 7/8 stays available to userland regardless.
 */
#define BIO_CACHE_RAM_SHIFT 3        /* total_ram >> 3 == 1/8 of RAM */
static uint32_t bio_nbuf_ceiling = BIO_NBUF_FLOOR;

static struct bufhashhead bio_hashtbl[BIO_HASH_SIZE];
static struct bufqueue bio_queues[BQ_COUNT];
static spinlock_t bio_lock;
static uint32_t bio_nbuf;
static uint64_t bio_resident_bytes;
static uint64_t bio_hits;
static uint64_t bio_misses;
static uint64_t bio_reclaims;

static inline uint32_t
bio_hash(struct vnode *vp, int64_t blkno)
{
    return (((uintptr_t)vp >> 4) ^ (uint64_t)blkno) % BIO_HASH_SIZE;
}

static void
bio_remove_from_queue(struct buf *bp)
{
    if (bp->b_qindex >= 0 && bp->b_qindex < BQ_COUNT) {
        TAILQ_REMOVE(&bio_queues[bp->b_qindex], bp, b_freelist);
    }
    bp->b_qindex = -1;
}

static void
bio_insert_queue(struct buf *bp, int qindex)
{
    bp->b_qindex = qindex;
    TAILQ_INSERT_TAIL(&bio_queues[qindex], bp, b_freelist);
}

static void
bio_hash_insert(struct buf *bp)
{
    uint32_t hash = bio_hash(bp->b_vp, bp->b_blkno);
    LIST_INSERT_HEAD(&bio_hashtbl[hash], bp, b_hash);
}

static void
bio_hash_remove(struct buf *bp)
{
    if (bp->b_vp == NULL)
        return;
    LIST_REMOVE(bp, b_hash);
}

static struct buf *
bio_lookup_locked(struct vnode *vp, int64_t blkno)
{
    struct buf *bp;
    uint32_t hash = bio_hash(vp, blkno);

    LIST_FOREACH(bp, &bio_hashtbl[hash], b_hash) {
        if (bp->b_vp == vp && bp->b_blkno == blkno)
            return bp;
    }

    return NULL;
}

static int
bio_ensure_size(struct buf *bp, size_t size)
{
    if (bp->b_data && bp->b_bcount == size) {
        bp->b_resid = 0;
        return 0;
    }

    /*
     * Past this point the buffer's contents do not survive: either we drop
     * the existing allocation, or there never was one and the allocation
     * below is zero-filled.  B_CACHE means "b_data holds this block's real
     * contents", so it must never outlive the data it describes -- leaving
     * it set here is what let a zero-filled page be handed to a reader as
     * the contents of a disk sector.
     */
    bp->b_flags &= ~B_CACHE;

    if (bp->b_data) {
        if (bio_resident_bytes >= bp->b_bcount)
            bio_resident_bytes -= bp->b_bcount;
        kfree(bp->b_data, bp->b_bcount);
        bp->b_data = NULL;
        bp->b_bcount = 0;
    }

    if (size == 0) {
        bp->b_resid = 0;
        return 0;
    }

    bp->b_data = kmalloc(size);
    if (!bp->b_data)
        return ENOMEM;
    memset(bp->b_data, 0, size);
    bp->b_bcount = size;
    bp->b_resid = 0;
    bio_resident_bytes += size;
    return 0;
}

static struct buf *
bio_reuse_candidate_locked(void)
{
    struct buf *bp;

    bp = TAILQ_FIRST(&bio_queues[BQ_EMPTY]);
    if (bp)
        return bp;

    bp = TAILQ_FIRST(&bio_queues[BQ_CLEAN]);
    if (bp)
        return bp;

    return NULL;
}

/*
 * Should we allow growing the pool by one more buffer of `size` bytes?
 *
 * Below the floor: always.
 * Above the ceiling: never.
 * In between: only if doing so leaves >= BIO_RESERVE_BYTES of free RAM.
 *
 * pmm_get_free_memory() is conservative (counts only direct-mapped pool)
 * which is exactly the pool kmalloc() pulls from.  Using it here avoids
 * over-committing the cache and starving the rest of the kernel.
 */
static int
bio_can_grow_locked(size_t size)
{
    uint32_t free_ram;

    if (bio_nbuf < BIO_NBUF_FLOOR)
        return 1;
    if (bio_nbuf >= bio_nbuf_ceiling)
        return 0;

    free_ram = pmm_get_free_memory();
    if ((uint64_t)free_ram < (uint64_t)size + BIO_RESERVE_BYTES)
        return 0;
    return 1;
}

void
bio_init(void)
{
    uint32_t i;
    thread_t *syncer_td;

    /* Size the cache ceiling to 1/8 of total RAM (see BIO_CACHE_RAM_SHIFT).
     * per-buffer cost ~= struct buf metadata + one sector of data. */
    {
        uint32_t total = pmm_get_total_memory();
        uint32_t per   = (uint32_t)sizeof(struct buf) + 512;
        uint64_t budget = (uint64_t)total >> BIO_CACHE_RAM_SHIFT;
        uint64_t ceil  = budget / per;
        if (ceil < BIO_NBUF_FLOOR)      ceil = BIO_NBUF_FLOOR;
        if (ceil > BIO_NBUF_HARD_CAP)   ceil = BIO_NBUF_HARD_CAP;
        bio_nbuf_ceiling = (uint32_t)ceil;
        kprintf("bio: block cache ceiling %u buffers (~%u MiB) of %u MiB RAM\n",
            bio_nbuf_ceiling,
            (unsigned)((uint64_t)bio_nbuf_ceiling * per / (1024U * 1024U)),
            total / (1024U * 1024U));
    }

    for (i = 0; i < BIO_HASH_SIZE; i++) {
        LIST_INIT(&bio_hashtbl[i]);
    }
    for (i = 0; i < BQ_COUNT; i++) {
        TAILQ_INIT(&bio_queues[i]);
    }

    spinlock_init(&bio_lock, "bio");
    bio_nbuf = 0;
    bio_resident_bytes = 0;
    bio_hits = 0;
    bio_misses = 0;
    bio_reclaims = 0;

    syncer_td = NULL;
    (void)kthread_create(syncer_daemon, NULL, &syncer_td, "syncer");
}

struct buf *
incore(struct vnode *vp, int64_t blkno)
{
    struct buf *bp;

    spinlock_acquire(&bio_lock);
    bp = bio_lookup_locked(vp, blkno);
    spinlock_release(&bio_lock);

    return bp;
}

/*
 * Detach a buffer that could not be given valid storage and park it on
 * BQ_EMPTY for reuse.  Mirrors brelse()'s B_INVAL arm, but callable from
 * inside getblk() while bio_lock is already held.
 *
 * The point is that a buffer with no data must not stay reachable through
 * the hash: a later lookup would find it, and any caller that trusts the
 * hit would read whatever the next allocation happens to contain.
 */
static void
bio_discard_locked(struct buf *bp)
{
    if (bp->b_qindex != -1)
        bio_remove_from_queue(bp);

    bp->b_flags &= ~(B_BUSY | B_CACHE);

    if (bp->b_vp) {
        bio_hash_remove(bp);
        bp->b_vp = NULL;
    }
    bp->b_blkno = 0;
    bp->b_lblkno = 0;

    if (bp->b_data) {
        if (bio_resident_bytes >= bp->b_bcount)
            bio_resident_bytes -= bp->b_bcount;
        kfree(bp->b_data, bp->b_bcount);
        bp->b_data = NULL;
        bp->b_bcount = 0;
    }
    bp->b_resid = 0;

    bio_insert_queue(bp, BQ_EMPTY);
}

struct buf *
getblk(struct vnode *vp, int64_t blkno, size_t size, int slpflag, int slptimeo)
{
    struct buf *bp;
    int error;

    (void)slpflag;
    (void)slptimeo;

retry_lookup:
    spinlock_acquire(&bio_lock);

    bp = bio_lookup_locked(vp, blkno);
    if (bp) {
        while (bp->b_flags & B_BUSY) {
            /* Block on the buffer's sleep channel.  Plain sched_yield()
             * would just reorder the runqueue (yield clears RUNNING→
             * READY but leaves us schedulable), giving us a busy-yield
             * loop on UP.  sched_sleep() sets THREAD_BLOCKED so we are
             * not reconsidered until sleepq_wake_all(bp) flips us
             * back to READY. */
            sleepq_add(bp, current_thread);
            spinlock_release(&bio_lock);
            sched_sleep(bp);
            spinlock_acquire(&bio_lock);
            bp = bio_lookup_locked(vp, blkno);
            if (!bp) {
                spinlock_release(&bio_lock);
                goto retry_lookup;
            }
        }

        if (bp->b_qindex != -1)
            bio_remove_from_queue(bp);

        /*
         * Do NOT assert B_CACHE here.  A hash hit only means some buffer is
         * keyed to this block, not that it ever held the block's contents --
         * a buffer whose fill failed can still be on the hash.  B_CACHE is
         * carried over from the previous successful fill, and bio_ensure_size()
         * clears it if it has to (re)allocate.
         */
        bp->b_flags |= B_BUSY;
        bp->b_flags &= ~(B_DONE | B_ERROR | B_ASYNC | B_READ | B_WRITE);
        bio_insert_queue(bp, BQ_LOCKED);

        error = bio_ensure_size(bp, size);
        if (error) {
            /* No storage: drop it rather than leave a dataless buffer on the
             * hash.  Callers treat NULL as "bypass the cache" and read the
             * device directly, which is correct here. */
            bio_discard_locked(bp);
            spinlock_release(&bio_lock);
            sleepq_wake_all(bp);
            return NULL;
        }

        bio_hits++;
        spinlock_release(&bio_lock);
        return bp;
    }

    bio_misses++;

    if (bio_can_grow_locked(size)) {
        bp = kmalloc(sizeof(*bp));
        if (!bp) {
            spinlock_release(&bio_lock);
            return NULL;
        }
        memset(bp, 0, sizeof(*bp));
        bp->b_qindex = -1;
        bio_nbuf++;
    } else {
        bp = bio_reuse_candidate_locked();
        if (!bp) {
            spinlock_release(&bio_lock);
            return NULL;
        }

        bio_remove_from_queue(bp);

        if (bp->b_vp) {
            bio_hash_remove(bp);
        }

        bp->b_vp = NULL;
        bp->b_blkno = 0;
        bp->b_lblkno = 0;
        bp->b_rcred = NULL;
        bp->b_wcred = NULL;
        bp->b_error = 0;
        bp->b_iodone = NULL;
        bp->b_flags = 0;
    }

    bp->b_vp = vp;
    bp->b_blkno = blkno;
    bp->b_lblkno = blkno;
    bp->b_flags = B_BUSY | B_READ;
    bp->b_error = 0;
    bp->b_resid = 0;

    error = bio_ensure_size(bp, size);
    if (error) {
        /* Never publish a buffer we could not back with storage: hashing it
         * here is what allowed a later lookup to hit it, force B_CACHE on,
         * and serve a zero-filled page as the block's contents.
         *
         * b_vp is set but bio_hash_insert() has not run yet, and
         * bio_hash_remove() does an unconditional LIST_REMOVE -- clear the
         * key first so the discard does not unlink an entry that was never
         * linked. */
        bp->b_vp = NULL;
        bio_discard_locked(bp);
        spinlock_release(&bio_lock);
        return NULL;
    }

    bio_hash_insert(bp);
    bio_insert_queue(bp, BQ_LOCKED);

    spinlock_release(&bio_lock);
    return bp;
}

int
biowait(struct buf *bp)
{
    int error;

    if (!bp)
        return EINVAL;

    spinlock_acquire(&bio_lock);
    while ((bp->b_flags & B_DONE) == 0) {
        sleepq_add(bp, current_thread);
        spinlock_release(&bio_lock);
        sched_yield();
        spinlock_acquire(&bio_lock);
    }

    error = (bp->b_flags & B_ERROR) ? (bp->b_error ? bp->b_error : EIO) : 0;
    spinlock_release(&bio_lock);

    return error;
}

void
biodone(struct buf *bp)
{
    biodone_t iodone;

    if (!bp)
        return;

    spinlock_acquire(&bio_lock);
    bp->b_flags |= B_DONE;
    iodone = bp->b_iodone;
    spinlock_release(&bio_lock);

    if (iodone)
        iodone(bp);

    sleepq_wake_all(bp);
}

int
bread(struct vnode *vp, int64_t blkno, size_t size, struct ucred *cred, struct buf **bpp)
{
    struct buf *bp;
    int error;

    if (!vp || !bpp)
        return EINVAL;

    bp = getblk(vp, blkno, size, 0, 0);
    if (!bp)
        return ENOMEM;

    bp->b_rcred = cred;
    *bpp = bp;

    if (bp->b_flags & B_CACHE) {
        bp->b_flags |= B_DONE;
        return 0;
    }

    bp->b_flags &= ~(B_DONE | B_ERROR | B_DELWRI);
    bp->b_flags |= B_READ;

    error = VOP_STRATEGY(vp, bp);
    if (error) {
        bp->b_error = error;
        bp->b_flags |= B_ERROR;
        biodone(bp);
    } else if ((bp->b_flags & B_DONE) == 0) {
        biodone(bp);
    }

    return biowait(bp);
}

int
breada(struct vnode *vp, int64_t blkno, size_t size,
       int64_t rablkno, size_t rabsize, struct ucred *cred, struct buf **bpp)
{
    struct buf *rabp;
    int error;

    error = bread(vp, blkno, size, cred, bpp);
    if (error)
        return error;

    if (rablkno < 0 || rabsize == 0)
        return 0;

    rabp = getblk(vp, rablkno, rabsize, 0, 0);
    if (!rabp)
        return 0;

    if ((rabp->b_flags & B_CACHE) == 0) {
        rabp->b_flags &= ~(B_DONE | B_ERROR | B_DELWRI);
        rabp->b_flags |= (B_READ | B_ASYNC);

        error = VOP_STRATEGY(vp, rabp);
        if (error) {
            rabp->b_error = error;
            rabp->b_flags |= B_ERROR;
        }
        biodone(rabp);
    }

    brelse(rabp);
    return 0;
}

int
bwrite(struct buf *bp)
{
    int error;
    int async;

    if (!bp || !bp->b_vp)
        return EINVAL;

    async = (bp->b_flags & B_ASYNC) != 0;

    bp->b_flags &= ~(B_DONE | B_ERROR | B_DELWRI | B_READ);
    bp->b_flags |= B_WRITE;

    error = VOP_STRATEGY(bp->b_vp, bp);
    if (error) {
        bp->b_error = error;
        bp->b_flags |= B_ERROR;
        biodone(bp);
    } else if ((bp->b_flags & B_DONE) == 0) {
        biodone(bp);
    }

    if (async) {
        brelse(bp);
        return 0;
    }

    error = biowait(bp);
    brelse(bp);
    return error;
}

int
bawrite(struct buf *bp)
{
    if (!bp)
        return EINVAL;

    bp->b_flags |= B_ASYNC;
    return bwrite(bp);
}

void
bdwrite(struct buf *bp)
{
    if (!bp)
        return;

    bp->b_flags |= B_DELWRI;
    bp->b_flags &= ~(B_DONE | B_ERROR | B_READ);
    bp->b_flags |= B_WRITE;

    brelse(bp);
}

void
brelse(struct buf *bp)
{
    if (!bp)
        return;

    spinlock_acquire(&bio_lock);

    if (bp->b_qindex == BQ_LOCKED)
        bio_remove_from_queue(bp);

    bp->b_flags &= ~B_BUSY;

    if (bp->b_flags & (B_INVAL | B_NOCACHE)) {
        /* Detach from hash + vnode key so the next lookup misses,
         * matching the semantics callers expect of B_INVAL. */
        if (bp->b_vp) {
            bio_hash_remove(bp);
            bp->b_vp = NULL;
        }
        bp->b_blkno = 0;
        bp->b_lblkno = 0;
        bp->b_flags &= ~B_CACHE;
        if (bp->b_data) {
            if (bio_resident_bytes >= bp->b_bcount)
                bio_resident_bytes -= bp->b_bcount;
            kfree(bp->b_data, bp->b_bcount);
            bp->b_data = NULL;
            bp->b_bcount = 0;
            bp->b_resid = 0;
        }
        bio_insert_queue(bp, BQ_EMPTY);
    } else if (bp->b_flags & B_DELWRI) {
        bio_insert_queue(bp, BQ_DIRTY);
    } else {
        bio_insert_queue(bp, BQ_CLEAN);
    }

    spinlock_release(&bio_lock);
    sleepq_wake_all(bp);
}

int
bufsync(int freq)
{
    struct buf *bp;
    int sync_error;

    (void)freq;

    sync_error = 0;

    while (1) {
        spinlock_acquire(&bio_lock);
        bp = TAILQ_FIRST(&bio_queues[BQ_DIRTY]);
        if (!bp) {
            spinlock_release(&bio_lock);
            break;
        }

        bio_remove_from_queue(bp);
        bp->b_flags |= B_BUSY;
        bp->b_qindex = BQ_LOCKED;
        TAILQ_INSERT_TAIL(&bio_queues[BQ_LOCKED], bp, b_freelist);
        spinlock_release(&bio_lock);

        bp->b_flags &= ~B_ASYNC;
        if (bwrite(bp) != 0 && sync_error == 0)
            sync_error = EIO;
    }

    return sync_error;
}

int
binval_vnode(struct vnode *vp, int save)
{
    struct buf *bp;
    struct buf *next;
    uint32_t i;
    int error;

    error = 0;

    for (i = 0; i < BIO_HASH_SIZE; i++) {
retry_bucket:
        spinlock_acquire(&bio_lock);
        LIST_FOREACH_SAFE(bp, &bio_hashtbl[i], b_hash, next) {
            if (bp->b_vp != vp)
                continue;

            if (bp->b_flags & B_BUSY) {
                sleepq_add(bp, current_thread);
                spinlock_release(&bio_lock);
                sched_yield();
                goto retry_bucket;
            }

            if (bp->b_qindex != -1)
                bio_remove_from_queue(bp);
            bp->b_flags |= B_BUSY;
            bio_insert_queue(bp, BQ_LOCKED);
            spinlock_release(&bio_lock);

            if ((bp->b_flags & B_DELWRI) && save) {
                if (bwrite(bp) != 0 && error == 0)
                    error = EIO;
            } else {
                bp->b_flags |= B_INVAL;
                bp->b_flags &= ~B_DELWRI;
                brelse(bp);
            }
            goto retry_bucket;
        }
        spinlock_release(&bio_lock);
    }

    return error;
}

void
syncer_daemon(void *arg)
{
    uint32_t hz;
    uint64_t period;
    uint64_t deadline;

    (void)arg;

    hz = get_hz();
    if (hz == 0)
        hz = 100;
    period = (uint64_t)hz * 30ULL;

    for (;;) {
        bufsync(30);
        deadline = get_ticks() + period;
        (void)sched_sleep_until(&bio_lock, deadline);
    }
}

/*
 * ============================================================
 * Device-keyed manual-fill API
 * ============================================================
 *
 * Used by raw-device readers (fs/ext2, fs/udf) that don't have a
 * struct vnode for their backing device but want to share the cache.
 * The opaque dev pointer is treated identically to a struct vnode *
 * for hashing purposes; bio.c never dereferences it.
 *
 * Caller flow on miss:
 *   bp = bio_dev_get(dev, blkno, size);
 *   if (!(bp->b_flags & B_CACHE)) {
 *       device_read_into(bp->b_data);
 *       bp->b_flags |= B_CACHE;
 *   }
 *   memcpy(out, bp->b_data, size);
 *   bio_dev_release(bp);
 */

struct buf *
bio_dev_get(void *dev, int64_t blkno, size_t size)
{
    return getblk((struct vnode *)dev, blkno, size, 0, 0);
}

void
bio_dev_release(struct buf *bp)
{
    brelse(bp);
}

void
bio_dev_mark_dirty(struct buf *bp)
{
    if (!bp)
        return;
    bp->b_flags |= B_DELWRI;
}

/*
 * Lock-safe presence probe: returns nonzero iff (dev, blkno) is resident,
 * valid (B_CACHE), and not currently busy.  The verdict is computed under
 * bio_lock and the buffer pointer never escapes, so this is safe to call
 * speculatively (e.g. the block layer's miss-run coalescing) without the
 * use-after-release hazard of incore().
 */
int
bio_dev_cached(void *dev, int64_t blkno)
{
    struct buf *bp;
    int cached;

    spinlock_acquire(&bio_lock);
    bp = bio_lookup_locked((struct vnode *)dev, blkno);
    cached = (bp && (bp->b_flags & B_CACHE) && !(bp->b_flags & B_BUSY));
    spinlock_release(&bio_lock);
    return cached;
}

void
bio_dev_invalidate(void *dev, int64_t blkno)
{
    struct buf *bp;

retry:
    spinlock_acquire(&bio_lock);
    bp = bio_lookup_locked((struct vnode *)dev, blkno);
    if (!bp) {
        spinlock_release(&bio_lock);
        return;
    }
    if (bp->b_flags & B_BUSY) {
        sleepq_add(bp, current_thread);
        spinlock_release(&bio_lock);
        sched_yield();
        goto retry;
    }
    if (bp->b_qindex != -1)
        bio_remove_from_queue(bp);
    bp->b_flags |= B_BUSY;
    bio_insert_queue(bp, BQ_LOCKED);
    spinlock_release(&bio_lock);

    if (bp->b_flags & B_DELWRI) {
        /*
         * FS-10: the block carries a pending delayed write.  The old code
         * cleared B_DELWRI unconditionally here, silently discarding that
         * writeback.  Flush it to the backing store first (bwrite() writes
         * the buffer and clears B_BUSY/B_DELWRI), so the write is not lost;
         * B_INVAL set beforehand makes the trailing brelse() inside bwrite()
         * still drop the now-stale slot from cache so the next read
         * re-fetches from disk (the invalidation this function owes).
         */
        bp->b_flags |= B_INVAL;
        (void)bwrite(bp);
    } else {
        bp->b_flags |= B_INVAL;
        bp->b_flags &= ~B_CACHE;
        brelse(bp);
    }
}

void
bio_dev_purge(void *dev)
{
    struct buf *bp;
    struct buf *next;
    uint32_t i;

    for (i = 0; i < BIO_HASH_SIZE; i++) {
retry_bucket:
        spinlock_acquire(&bio_lock);
        LIST_FOREACH_SAFE(bp, &bio_hashtbl[i], b_hash, next) {
            if (bp->b_vp != (struct vnode *)dev)
                continue;
            if (bp->b_flags & B_BUSY) {
                sleepq_add(bp, current_thread);
                spinlock_release(&bio_lock);
                sched_yield();
                goto retry_bucket;
            }
            if (bp->b_qindex != -1)
                bio_remove_from_queue(bp);
            bp->b_flags |= B_BUSY | B_INVAL;
            bp->b_flags &= ~(B_DELWRI | B_CACHE);
            bio_insert_queue(bp, BQ_LOCKED);
            spinlock_release(&bio_lock);
            brelse(bp);
            goto retry_bucket;
        }
        spinlock_release(&bio_lock);
    }
}

/*
 * ============================================================
 * Reclaim
 * ============================================================
 */

size_t
bio_reclaim(size_t target_bytes)
{
    struct buf *bp;
    struct buf *next;
    size_t freed;
    size_t bsize;

    freed = 0;
    spinlock_acquire(&bio_lock);

    /* Drain BQ_EMPTY first — these have no data so we mainly recover bp
     * metadata and slot count. */
    TAILQ_FOREACH_SAFE(bp, &bio_queues[BQ_EMPTY], b_freelist, next) {
        if (bp->b_flags & B_BUSY)
            continue;
        bio_remove_from_queue(bp);
        if (bp->b_vp)
            bio_hash_remove(bp);
        if (bp->b_data) {
            bsize = bp->b_bcount;
            if (bio_resident_bytes >= bsize)
                bio_resident_bytes -= bsize;
            kfree(bp->b_data, bsize);
            freed += bsize;
        }
        kfree(bp, sizeof(*bp));
        if (bio_nbuf > 0)
            bio_nbuf--;
        if (target_bytes && freed >= target_bytes)
            goto done;
    }

    /* Then BQ_CLEAN in LRU order (head = oldest). */
    TAILQ_FOREACH_SAFE(bp, &bio_queues[BQ_CLEAN], b_freelist, next) {
        if (bp->b_flags & B_BUSY)
            continue;
        bio_remove_from_queue(bp);
        if (bp->b_vp)
            bio_hash_remove(bp);
        if (bp->b_data) {
            bsize = bp->b_bcount;
            if (bio_resident_bytes >= bsize)
                bio_resident_bytes -= bsize;
            kfree(bp->b_data, bsize);
            freed += bsize;
        }
        kfree(bp, sizeof(*bp));
        if (bio_nbuf > 0)
            bio_nbuf--;
        if (target_bytes && freed >= target_bytes)
            goto done;
    }

done:
    bio_reclaims++;
    spinlock_release(&bio_lock);
    return freed;
}

void
bio_get_stats(struct bio_stats *out)
{
    struct buf *bp;

    if (!out)
        return;

    memset(out, 0, sizeof(*out));

    spinlock_acquire(&bio_lock);
    out->nbuf = bio_nbuf;
    out->resident_bytes = bio_resident_bytes;
    out->hits = bio_hits;
    out->misses = bio_misses;
    out->reclaims = bio_reclaims;

    TAILQ_FOREACH(bp, &bio_queues[BQ_LOCKED], b_freelist)
        out->q_locked++;
    TAILQ_FOREACH(bp, &bio_queues[BQ_CLEAN], b_freelist)
        out->q_clean++;
    TAILQ_FOREACH(bp, &bio_queues[BQ_DIRTY], b_freelist)
        out->q_dirty++;
    TAILQ_FOREACH(bp, &bio_queues[BQ_EMPTY], b_freelist)
        out->q_empty++;
    spinlock_release(&bio_lock);

    out->free_ram_bytes = pmm_get_free_memory();
    out->nbuf_target = (out->free_ram_bytes > BIO_RESERVE_BYTES) ?
        bio_nbuf_ceiling : BIO_NBUF_FLOOR;
}
