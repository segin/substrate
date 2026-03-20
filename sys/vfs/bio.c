#include <vfs/buf.h>
#include <vfs/vnode.h>
#include <sys/lock.h>
#include <sys/errno.h>
#include <sys/kthread.h>
#include <kern/sched.h>
#include <kern/sleepq.h>
#include <kern/time.h>
#include <vm/vm_kmem.h>
#include <string.h>

#define BIO_HASH_SIZE   256
#define BIO_NBUF_MAX    512

static struct bufhashhead bio_hashtbl[BIO_HASH_SIZE];
static struct bufqueue bio_queues[BQ_COUNT];
static spinlock_t bio_lock;
static uint32_t bio_nbuf;

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

    if (bp->b_data) {
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

void
bio_init(void)
{
    uint32_t i;
    thread_t *syncer_td;

    for (i = 0; i < BIO_HASH_SIZE; i++) {
        LIST_INIT(&bio_hashtbl[i]);
    }
    for (i = 0; i < BQ_COUNT; i++) {
        TAILQ_INIT(&bio_queues[i]);
    }

    spinlock_init(&bio_lock, "bio");
    bio_nbuf = 0;

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
            sleepq_add(bp, current_thread);
            spinlock_release(&bio_lock);
            sched_yield();
            spinlock_acquire(&bio_lock);
            bp = bio_lookup_locked(vp, blkno);
            if (!bp) {
                spinlock_release(&bio_lock);
                goto retry_lookup;
            }
        }

        if (bp->b_qindex != -1)
            bio_remove_from_queue(bp);

        bp->b_flags |= B_BUSY | B_CACHE;
        bp->b_flags &= ~(B_DONE | B_ERROR | B_ASYNC | B_READ | B_WRITE);
        bio_insert_queue(bp, BQ_LOCKED);

        error = bio_ensure_size(bp, size);
        if (error) {
            bp->b_error = error;
            bp->b_flags |= B_ERROR;
        }

        spinlock_release(&bio_lock);
        return bp;
    }

    if (bio_nbuf < BIO_NBUF_MAX) {
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
        bp->b_error = error;
        bp->b_flags |= B_ERROR;
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
        if (bp->b_data) {
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