/*
 * vnode.c - Virtual Node lifecycle management
 *
 * Implements BSD-style vnode lifecycle:
 * - getnewvnode(): Allocate new vnode from zone, recycle LRU if full
 * - vref(): Increment use count
 * - vrele(): Decrement use count, trigger inactive/reclaim if zero
 * - vput(): Unlock and vrele
 * - vget(): Lock and vref
 * - vgone(): Mark vnode for destruction
 * - vclean(): Disassociate vnode from filesystem
 *
 * Lock ordering (acquire in this order to avoid deadlocks):
 *   1. vnode lock (lockmgr per-vnode)
 *   2. vnode_freelist_lock (spinlock, protects freelist/LRU)
 *   3. bio_lock (spinlock, protects buffer cache hash/queues)
 *   4. nchash_lock (rwlock, protects name cache)
 *
 * Never acquire a higher-numbered lock while holding a lower-numbered one.
 * Per-vnode locks are ordered by address to avoid A-B / B-A deadlocks.
 */

#include <string.h>

#include <kern/console.h>
#include <kern/panic.h>
#include <kern/sched.h>
#include <kern/sleepq.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <vfs/buf.h>
#include <vfs/vnode.h>
#include <vm/uma.h>

/* Vnode zone for allocation */
static uma_zone_t *vnode_zone;

/* Free list for vnode recycling (LRU) */
static struct vnode *vnode_freelist_head;
static struct vnode *vnode_freelist_tail;
static spinlock_t vnode_freelist_lock;

/* Hash table for vnode lookup by mount/ino */
#define VNODE_HASH_SIZE 256
static struct vnode *vnode_hashtable[VNODE_HASH_SIZE];
static spinlock_t vnode_hash_lock;

/* Protects every mp->mnt_vnodelist [VNODE-23]. */
static spinlock_t vnode_mntlist_lock;

/* Statistics */
struct vnode_stats vnstats;

/* Maximum number of vnodes in system */
#define MAXVNODES 4096
static uint32_t numvnodes = 0;

/* Hash function for vnode lookup */
static inline uint32_t vnode_hash(struct mount *mp, uint64_t ino)
{
    return ((uintptr_t)mp ^ ino) % VNODE_HASH_SIZE;
}

/*
 * Initialize vnode subsystem
 */
void vnode_init(void)
{
    kprint("VNODE: Initializing vnode subsystem...\n");
    
    /* Initialize UMA zone for vnodes */
    vnode_zone = uma_zcreate("vnode", sizeof(struct vnode),
                             NULL, NULL, NULL, NULL,
                             0, 0);  /* align=0 means default pointer alignment */
    if (!vnode_zone) {
        panic("vnode_init: failed to create vnode zone");
    }
    
    /* Initialize free list */
    vnode_freelist_head = NULL;
    vnode_freelist_tail = NULL;
    spinlock_init(&vnode_freelist_lock, "vnode_freelist");
    
    /* Initialize hash table */
    memset(vnode_hashtable, 0, sizeof(vnode_hashtable));
    spinlock_init(&vnode_hash_lock, "vnode_hash");
    spinlock_init(&vnode_mntlist_lock, "vnode_mntlist");
    
    /* Initialize stats */
    memset(&vnstats, 0, sizeof(vnstats));
    
    kprint("VNODE: Ready.\n");
}

/*
 * Add vnode to free list tail (LRU order)
 */
static void vnode_freelist_add(struct vnode *vp)
{
    spinlock_acquire(&vnode_freelist_lock);
    
    vp->v_flag |= VONFREELIST;
    vp->v_freelist_prev = vnode_freelist_tail;
    vp->v_freelist_next = NULL;
    
    if (vnode_freelist_tail) {
        vnode_freelist_tail->v_freelist_next = vp;
    } else {
        vnode_freelist_head = vp;
    }
    vnode_freelist_tail = vp;
    
    vnstats.freevnodes++;
    spinlock_release(&vnode_freelist_lock);
}

/*
 * Unlink from the free list.  Caller holds vnode_freelist_lock.
 *
 * [VNODE-19] Split out so that vref() and vnode_recycle() can decide to take
 * a vnode and unlink it WITHOUT dropping the freelist lock in between.  The
 * lock order for all of this is vnode_freelist_lock -> v_interlock; nothing
 * may take the freelist lock while already holding a v_interlock.
 */
static void vnode_freelist_unlink_locked(struct vnode *vp)
{
    if (vp->v_freelist_prev) {
        vp->v_freelist_prev->v_freelist_next = vp->v_freelist_next;
    } else {
        vnode_freelist_head = vp->v_freelist_next;
    }

    if (vp->v_freelist_next) {
        vp->v_freelist_next->v_freelist_prev = vp->v_freelist_prev;
    } else {
        vnode_freelist_tail = vp->v_freelist_prev;
    }

    vp->v_freelist_prev = NULL;
    vp->v_freelist_next = NULL;
    vp->v_flag &= ~VONFREELIST;
    vnstats.freevnodes--;
}

/*
 * Remove vnode from free list
 */
static void vnode_freelist_remove(struct vnode *vp)
{
    spinlock_acquire(&vnode_freelist_lock);

    if (!(vp->v_flag & VONFREELIST)) {
        spinlock_release(&vnode_freelist_lock);
        return;
    }

    if (vp->v_freelist_prev) {
        vp->v_freelist_prev->v_freelist_next = vp->v_freelist_next;
    } else {
        vnode_freelist_head = vp->v_freelist_next;
    }
    
    if (vp->v_freelist_next) {
        vp->v_freelist_next->v_freelist_prev = vp->v_freelist_prev;
    } else {
        vnode_freelist_tail = vp->v_freelist_prev;
    }
    
    vp->v_freelist_prev = NULL;
    vp->v_freelist_next = NULL;
    vp->v_flag &= ~VONFREELIST;
    
    vnstats.freevnodes--;
    spinlock_release(&vnode_freelist_lock);
}

/*
 * Recycle a vnode from the free list
 * Returns locked vnode ready for reuse
 */
static struct vnode *vnode_recycle(void)
{
    struct vnode *vp;
    
    spinlock_acquire(&vnode_freelist_lock);

    /*
     * Get oldest (head) vnode from free list.
     *
     * [VNODE-19] Each candidate is examined under its OWN v_interlock, and
     * v_usecount is part of the test.  This scanned the list holding only
     * vnode_freelist_lock and never looked at v_usecount at all, while
     * vref() bumped v_usecount under v_interlock and then RELEASED it before
     * unlinking from the freelist -- two disjoint locks with no ordering
     * between them.  In that window a vnode that a concurrent lookup had
     * just referenced was still VONFREELIST with usecount 1, so this loop
     * would happily take it and vclean() it out from under the live caller.
     * Holding the freelist lock across the claim closes the window, and the
     * lock order (freelist before interlock) is the one vref() now follows.
     */
    vp = vnode_freelist_head;
    while (vp) {
        spinlock_acquire(&vp->v_interlock);
        if (vp->v_usecount == 0 && vp->v_holdcount == 0 &&
            !(vp->v_flag & VDOOMED)) {
            break;      /* claimed, still holding v_interlock */
        }
        spinlock_release(&vp->v_interlock);
        vp = vp->v_freelist_next;
    }

    if (!vp) {
        spinlock_release(&vnode_freelist_lock);
        return NULL;
    }

    /* Remove from free list; both locks still held, so nobody can grab it. */
    vnode_freelist_unlink_locked(vp);

    spinlock_release(&vp->v_interlock);
    spinlock_release(&vnode_freelist_lock);

    /*
     * [VNODE-16] Remove from the hash BEFORE vclean().
     *
     * vnode_recycle() never did this at all, and it cannot be deferred:
     * vnode_cache_remove() early-returns on !v_mount, and vclean() is what
     * clears v_mount -- so after the clean the entry could never be removed.
     * getnewvnode() then memset v_hash_next, truncating the bucket chain
     * (orphaning every later entry, so the same file gets duplicate vnodes)
     * and, once the vnode is re-inserted into that same bucket, closing it
     * into a CYCLE that hangs the next lookup miss with vnode_hash_lock held.
     *
     * [VNODE-17] And purge the name cache, which vnode_reclaim() already
     * does for exactly the same reason.  Without it the namecache keeps
     * mapping the OLD path to this vnode, so a lookup of one file can return
     * the vnode now backing a completely different one.
     */
    vnode_cache_remove(vp);
    cache_purge(vp);

    /* Clean the vnode for reuse */
    vclean(vp, 0);

    vnstats.vnode_recycle++;
    return vp;
}

/*
 * getnewvnode - Allocate a new vnode
 *
 * tag: filesystem identifier string (for debugging)
 * mp: mount point this vnode belongs to
 * vops: operations vector
 * vpp: output - newly allocated vnode
 *
 * Returns 0 on success, error code on failure
 */
int getnewvnode(const char *tag, struct mount *mp,
                struct vnodeops *vops, struct vnode **vpp)
{
    struct vnode *vp;
    
    (void)tag;  /* Currently unused, for debugging */
    
    /* Try to recycle from free list if we're at capacity */
    if (numvnodes >= MAXVNODES) {
        vp = vnode_recycle();
        if (!vp) {
            /* No recyclable vnodes available */
            kprint("getnewvnode: out of vnodes\n");
            return -ENOMEM;
        }
    } else {
        /* Allocate new vnode from zone */
        vp = uma_zalloc(vnode_zone, 0);
        if (!vp) {
            return -ENOMEM;
        }
        numvnodes++;
        vnstats.numvnodes = numvnodes;
        vnstats.vnode_alloc++;
    }
    
    /* Initialize vnode */
    memset(vp, 0, sizeof(*vp));
    vp->v_type = VNON;
    vp->v_tag = VT_NON;
    vp->v_op = vops;
    vp->v_mount = mp;
    vp->v_usecount = 1;  /* Start with one reference */
    vp->v_holdcount = 0;
    vp->v_writecount = 0;
    vp->v_flag = 0;
    /* memset made this 0, which is a VALID bucket index -- the "not hashed"
     * sentinel is -1 and has to be restored explicitly. */
    vp->v_hash_bucket = -1;
    spinlock_init(&vp->v_interlock, "vnode_interlock");
    lockinit(&vp->v_lock, 0, "vnode", 0);

    /*
     * [VNODE-23] Put the vnode on its mount's vnode list.  mnt_vnodelist was
     * TAILQ_INIT'ed and walked by vflush(), but nothing ever inserted into
     * it -- so the unmount busy check always passed and vflush() always
     * returned 0, i.e. a filesystem with live vnodes could be unmounted out
     * from under them.  vnode_reclaim() does the matching removal.
     */
    if (mp) {
        spinlock_acquire(&vnode_mntlist_lock);
        TAILQ_INSERT_TAIL(&mp->mnt_vnodelist, vp, v_mntlist);
        vp->v_flag |= VONMNTLIST;
        spinlock_release(&vnode_mntlist_lock);
    }

    *vpp = vp;
    return 0;
}

/*
 * vref - Increment vnode use count
 *
 * Called when acquiring a new reference to a vnode
 */
void vref(struct vnode *vp)
{
    if (!vp) {
        panic("vref: NULL vnode");
        return;
    }
    /*
     * [VNODE-19] Take the freelist lock FIRST, then v_interlock, and do the
     * whole bump-and-unlink under both.  The old code bumped v_usecount
     * under v_interlock, released it, and only then took the freelist lock
     * to unlink -- leaving the vnode referenced but still on the freelist,
     * where vnode_recycle() could claim and clean it.  This is the canonical
     * order; vnode_recycle() takes the same two locks the same way round.
     */
    spinlock_acquire(&vnode_freelist_lock);
    spinlock_acquire(&vp->v_interlock);

    vp->v_usecount++;

    /* Remove from free list if it was on it (transition from 0->1) */
    if (vp->v_usecount == 1 && (vp->v_flag & VONFREELIST)) {
        vnode_freelist_unlink_locked(vp);
    }

    spinlock_release(&vp->v_interlock);
    spinlock_release(&vnode_freelist_lock);
}

/*
 * vrele - Release a vnode reference
 *
 * Decrements use count. If it reaches zero, calls VOP_INACTIVE
 * and adds vnode to free list for potential recycling.
 */
void vrele(struct vnode *vp)
{
    if (!vp) {
        panic("vrele: NULL vnode");
        return;
    }
    spinlock_acquire(&vp->v_interlock);
    
    if (vp->v_usecount == 0) {
        panic("vrele: usecount already zero");
    }
    
    vp->v_usecount--;
    
    if (vp->v_usecount == 0) {
        /* Vnode is no longer in active use */

        /* Call VOP_INACTIVE if defined.  We must drop v_interlock
         * across the call because vop_inactive may sleep, so other
         * threads can race and re-vref() this vnode in the window. */
        if (vp->v_op && vp->v_op->vop_inactive) {
            spinlock_release(&vp->v_interlock);
            vp->v_op->vop_inactive(vp, NULL);
            spinlock_acquire(&vp->v_interlock);

            /* Re-check usecount: someone may have re-grabbed the
             * vnode while we were inactive — in which case it now
             * belongs to that caller, not the freelist. */
            if (vp->v_usecount > 0) {
                spinlock_release(&vp->v_interlock);
                return;
            }
        }

        /*
         * [VNODE-18] Reclaim only when there is no HOLD outstanding either.
         *
         * This tested v_usecount alone, while vgone() correctly requires
         * v_usecount == 0 && v_holdcount == 0.  A vhold() followed by
         * vgone() and vrele() therefore freed the vnode with a hold still
         * live -- the holder's next dereference is a use-after-free.  A held
         * doomed vnode goes to the freelist instead; vnode_recycle() already
         * skips entries with v_holdcount != 0, and the last vdrop() reclaims
         * it.
         */
        if ((vp->v_flag & VDOOMED) && vp->v_holdcount == 0) {
            spinlock_release(&vp->v_interlock);
            vnode_reclaim(vp);
            return; /* vp is freed by vnode_reclaim, do not touch */
        }

        /* Add to free list for potential recycling */
        spinlock_release(&vp->v_interlock);
        vnode_freelist_add(vp);
        return;
    }
    
    spinlock_release(&vp->v_interlock);
}

/*
 * vhold - Add a hold (weak reference) to vnode
 *
 * Holds prevent vnode recycling but don't count as active use.
 * Used for caching vnodes that might be needed again.
 */
void vhold(struct vnode *vp)
{
    spinlock_acquire(&vp->v_interlock);
    vp->v_holdcount++;
    spinlock_release(&vp->v_interlock);
}

/*
 * vdrop - Release a hold from vnode
 */
void vdrop(struct vnode *vp)
{
    spinlock_acquire(&vp->v_interlock);
    
    if (vp->v_holdcount == 0) {
        panic("vdrop: holdcount already zero");
    }
    
    vp->v_holdcount--;
    spinlock_release(&vp->v_interlock);
}

/*
 * vn_lock - Lock a vnode
 *
 * flags: LK_SHARED, LK_EXCLUSIVE, LK_NOWAIT
 * Returns 0 on success, error on failure
 */
int vn_lock(struct vnode *vp, int flags)
{
    int error;
    uint32_t op;

    if (flags & LK_SHARED)
        op = LK_SHARED;
    else
        op = LK_EXCLUSIVE;

    if (flags & LK_NOWAIT)
        op |= LK_NOWAIT;

    error = lockmgr(&vp->v_lock, op, &vp->v_interlock);

    if (error && (flags & LK_RETRY) && !(flags & LK_NOWAIT)) {
        /* Retry: loop until we get the lock */
        while (error) {
            sched_yield();
            error = lockmgr(&vp->v_lock, op, &vp->v_interlock);
        }
    }

    return error ? -EAGAIN : 0;
}

/*
 * vn_unlock - Unlock a vnode
 */
void vn_unlock(struct vnode *vp)
{
    lockmgr(&vp->v_lock, LK_RELEASE, &vp->v_interlock);
}

/*
 * vn_islocked - Check if vnode is locked
 */
int vn_islocked(struct vnode *vp)
{
    int status = lockstatus(&vp->v_lock);
    if (status == LK_EXCLUSIVE) return 2;
    if (status == LK_SHARED) return 1;
    return 0;
}

/*
 * vget - Get a locked reference to a vnode
 *
 * Combines vref and vn_lock into one operation.
 * Used when retrieving a vnode from cache.
 */
int vget(struct vnode *vp, int flags)
{
    int error;
    
    /* First lock the vnode */
    error = vn_lock(vp, flags);
    if (error) {
        return error;
    }
    
    /* Then add a reference */
    vref(vp);
    
    return 0;
}

/*
 * vput - Unlock and release a vnode
 *
 * Combines vn_unlock and vrele into one operation.
 */
void vput(struct vnode *vp)
{
    vn_unlock(vp);
    vrele(vp);
}

/*
 * vgone - Mark vnode for destruction
 *
 * Called when a file is deleted but vnode still has references.
 * The vnode will be reclaimed when references drop to zero.
 */
void vgone(struct vnode *vp)
{
    spinlock_acquire(&vp->v_interlock);
    
    /* Mark as doomed */
    vp->v_flag |= VDOOMED;
    
    /* Remove from hash table */
    spinlock_release(&vp->v_interlock);
    vnode_cache_remove(vp);
    spinlock_acquire(&vp->v_interlock);
    
    /* If no references, reclaim immediately */
    if (vp->v_usecount == 0 && vp->v_holdcount == 0) {
        spinlock_release(&vp->v_interlock);
        vnode_reclaim(vp);
        return;
    }
    
    spinlock_release(&vp->v_interlock);
}

/*
 * vclean - Disassociate vnode from filesystem data
 *
 * Called during recycling or destruction to clean up fs-specific state.
 */
void vclean(struct vnode *vp, int flags)
{
    (void)flags;
    
    spinlock_acquire(&vp->v_interlock);
    
    vp->v_flag |= VFREEING;
    
    /* Call VOP_RECLAIM if defined */
    if (vp->v_op && vp->v_op->vop_reclaim) {
        spinlock_release(&vp->v_interlock);
        vp->v_op->vop_reclaim(vp, NULL);
        spinlock_acquire(&vp->v_interlock);
    }
    
    /* Clear filesystem data */
    vp->v_data = NULL;
    vp->v_op = NULL;
    vp->v_mount = NULL;
    vp->v_type = VBAD;
    
    vp->v_flag &= ~(VFREEING | VDOOMED);
    
    spinlock_release(&vp->v_interlock);
}

/*
 * vinvalbuf - Invalidate all buffers for a vnode
 *
 * flags: V_SAVE - sync dirty data before invalidating
 */
int vinvalbuf(struct vnode *vp, int flags)
{
    int error;

    error = binval_vnode(vp, (flags & V_SAVE) ? 1 : 0);

    spinlock_acquire(&vp->v_interlock);
    vp->v_numoutput = 0;
    spinlock_release(&vp->v_interlock);

    return error;
}

/*
 * vflush - Flush all vnodes for a mount point
 *
 * mp: mount point to flush
 * skipvp: vnode to skip (usually root vnode during unmount)
 * flags: FORCECLOSE - force close even if busy
 *
 * Returns 0 on success, EBUSY if active vnodes remain.
 */
int vflush(struct mount *mp, struct vnode *skipvp, int flags)
{
    struct vnode *vp, *nvp;
    int busy = 0;

    /*
     * Walk the mount's vnode list. We use the safe variant since
     * vrele/vgone may remove vnodes from the list.
     */
    TAILQ_FOREACH_SAFE(vp, &mp->mnt_vnodelist, v_mntlist, nvp) {
        /* Skip the designated vnode (usually root) */
        if(vp == skipvp)
            continue;

        spinlock_acquire(&vp->v_interlock);

        /*
         * If vnode has active references and we're not forcing,
         * mark as busy and skip.
         */
        if(vp->v_usecount > 0 && !(flags & FORCECLOSE)) {
            busy++;
            spinlock_release(&vp->v_interlock);
            continue;
        }

        spinlock_release(&vp->v_interlock);

        /* Invalidate buffers for this vnode */
        vinvalbuf(vp, 0);

        /* Mark for doom and clean */
        vgone(vp);
    }

    if(busy)
        return(-EBUSY);

    return(0);
}

/*
 * vnode_reclaim - Final destruction of a vnode
 *
 * Called when vnode has no references and should be freed.
 */
void vnode_reclaim(struct vnode *vp)
{
    /* Drop every namecache entry that references this vnode (as either the
     * directory or the target) BEFORE the vnode memory is freed below.  The
     * namecache stores raw vnode pointers without a reference, so a stale
     * entry left here would make the next cache_lookup() vref() freed
     * memory -- a use-after-free reachable by ordinary file-access churn.
     * cache_purge() is otherwise only called from the remove/rename paths. */
    cache_purge(vp);

    /*
     * [VNODE-23] Unlink from the mount's vnode list BEFORE the memory goes
     * back to the zone.  getnewvnode() now inserts here, and vflush() walks
     * this list -- without the matching removal, vflush() would walk freed
     * vnodes.  Note this must happen before vclean() nulls v_mount, but the
     * VONMNTLIST flag makes it independent of v_mount either way.
     */
    if (vp->v_flag & VONMNTLIST) {
        spinlock_acquire(&vnode_mntlist_lock);
        if (vp->v_flag & VONMNTLIST) {
            TAILQ_REMOVE(&vp->v_mount->mnt_vnodelist, vp, v_mntlist);
            vp->v_flag &= ~VONMNTLIST;
        }
        spinlock_release(&vnode_mntlist_lock);
    }

    /* Clean up any remaining state */
    vclean(vp, 0);

    /* Remove from free list if present */
    vnode_freelist_remove(vp);

    /* Remove from hash table */
    vnode_cache_remove(vp);

    /* Free back to zone */
    uma_zfree(vnode_zone, vp);
    numvnodes--;
    vnstats.numvnodes = numvnodes;
    vnstats.vnode_free++;
}

/*
 * vnode_cache_insert - Insert vnode into lookup cache
 */
void vnode_cache_insert(struct vnode *vp)
{
    uint32_t hash;

    /*
     * [VNODE-16] Only v_ino has to be meaningful.  This also required a
     * non-NULL v_mount, which silently made the cache a no-op for any vnode
     * without one -- the fs_node_t bridge publishes exactly those, so every
     * lookup allocated a fresh vnode and none was ever found again.
     */
    if (vp->v_ino == 0) {
        return;  /* no usable key */
    }

    hash = vnode_hash(vp->v_mount, vp->v_ino);

    spinlock_acquire(&vnode_hash_lock);
    if (vp->v_hash_bucket >= 0) {
        /* Already hashed; do not link it in twice (that closes the bucket
         * into a cycle). */
        spinlock_release(&vnode_hash_lock);
        return;
    }
    vp->v_hash_next = vnode_hashtable[hash];
    vnode_hashtable[hash] = vp;
    vp->v_hash_bucket = (int32_t)hash;
    spinlock_release(&vnode_hash_lock);
}

/*
 * vnode_cache_remove - Remove vnode from lookup cache
 */
void vnode_cache_remove(struct vnode *vp)
{
    struct vnode **pp;

    /*
     * [VNODE-16] Unlink from the bucket the vnode was actually inserted
     * into, recorded at insert time.  This used to recompute the bucket from
     * (v_mount, v_ino) and bail out entirely when v_mount was NULL -- but
     * vclean() nulls v_mount, so once a vnode had been cleaned it could
     * never be unhashed, and the next getnewvnode() memset v_hash_next while
     * the vnode was still linked, truncating the chain and eventually
     * closing it into a cycle.
     */
    spinlock_acquire(&vnode_hash_lock);
    if (vp->v_hash_bucket < 0) {
        spinlock_release(&vnode_hash_lock);
        return;         /* not hashed */
    }

    for (pp = &vnode_hashtable[vp->v_hash_bucket]; *pp;
         pp = &(*pp)->v_hash_next) {
        if (*pp == vp) {
            *pp = vp->v_hash_next;
            break;
        }
    }
    vp->v_hash_next = NULL;
    vp->v_hash_bucket = -1;
    spinlock_release(&vnode_hash_lock);
}

/*
 * vnode_lookup_cache - Look up a vnode by mount/ino
 *
 * Returns vnode with incremented reference count, or NULL if not found.
 */
struct vnode *vnode_lookup_cache(struct mount *mp, uint64_t ino)
{
    uint32_t hash = vnode_hash(mp, ino);
    struct vnode *vp;
    
    spinlock_acquire(&vnode_hash_lock);
    for (vp = vnode_hashtable[hash]; vp; vp = vp->v_hash_next) {
        if (vp->v_mount == mp && vp->v_ino == ino) {
            /* Found - add reference */
            spinlock_release(&vnode_hash_lock);
            vref(vp);
            return vp;
        }
    }
    spinlock_release(&vnode_hash_lock);
    
    return NULL;
}

/*
 * vnode_create - High-level vnode creation helper
 */
int vnode_create(enum vtype type, struct mount *mp,
                 struct vnodeops *ops, struct vnode **vpp)
{
    int error;
    struct vnode *vp;
    
    error = getnewvnode("vnode", mp, ops, &vp);
    if (error) {
        return error;
    }
    
    vp->v_type = type;
    *vpp = vp;
    return 0;
}
