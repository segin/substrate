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
 */

#include <vfs/vnode.h>
#include <kern/console.h>
#include <kern/panic.h>
#include <kern/sched.h>
#include <kern/sleepq.h>
#include <sys/lock.h>
#include <sys/errno.h>
#include <vm/uma.h>
#include <string.h>

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
    
    /* Get oldest (head) vnode from free list */
    vp = vnode_freelist_head;
    while (vp) {
        /* Skip vnodes with holds or that are doomed */
        if (vp->v_holdcount == 0 && !(vp->v_flag & VDOOMED)) {
            break;
        }
        vp = vp->v_freelist_next;
    }
    
    if (!vp) {
        spinlock_release(&vnode_freelist_lock);
        return NULL;
    }
    
    /* Remove from free list */
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
            return -12; /* ENOMEM */
        }
    } else {
        /* Allocate new vnode from zone */
        vp = uma_zalloc(vnode_zone, 0);
        if (!vp) {
            return -12; /* ENOMEM */
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
    spinlock_init(&vp->v_interlock, "vnode_interlock");
    vp->v_lockstate = 0;
    vp->v_lockowner = NULL;
    
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
    spinlock_acquire(&vp->v_interlock);
    
    /* Remove from free list if on it */
    if (vp->v_usecount == 0 && (vp->v_flag & VONFREELIST)) {
        spinlock_release(&vp->v_interlock);
        vnode_freelist_remove(vp);
        spinlock_acquire(&vp->v_interlock);
    }
    
    vp->v_usecount++;
    
    spinlock_release(&vp->v_interlock);
}

/*
 * vrele - Release a vnode reference
 *
 * Decrements use count. If it reaches zero, calls VOP_INACTIVE
 * and adds vnode to free list for potential recycling.
 */
void vrele(struct vnode *vp)
{
    spinlock_acquire(&vp->v_interlock);
    
    if (vp->v_usecount == 0) {
        panic("vrele: usecount already zero");
    }
    
    vp->v_usecount--;
    
    if (vp->v_usecount == 0) {
        /* Vnode is no longer in active use */
        
        /* Call VOP_INACTIVE if defined */
        if (vp->v_op && vp->v_op->vop_inactive) {
            spinlock_release(&vp->v_interlock);
            vp->v_op->vop_inactive(vp, NULL);
            spinlock_acquire(&vp->v_interlock);
        }
        
        /* If doomed, reclaim immediately */
        if (vp->v_flag & VDOOMED) {
            spinlock_release(&vp->v_interlock);
            vnode_reclaim(vp);
            return;
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
    spinlock_acquire(&vp->v_interlock);

    if (flags & LK_SHARED) {
        /* Try to acquire shared lock */
        while (1) {
            /* If locked exclusively or someone wants exclusive (and we are not owner), wait */
            if ((vp->v_flag & VXLOCK) || (vp->v_flag & VXWANT)) {
                if ((vp->v_flag & VXLOCK) && (vp->v_lockowner == current_thread)) {
                    /* Recursive lock by exclusive owner -> downgrade or error?
                     * For now, error/no-op recursive logic.
                     * BSD allows recursive shared.
                     */
                     /* Currently return EDEADLK for simplicity or if we don't support recursion yet */
                    spinlock_release(&vp->v_interlock);
                    return -EDEADLK;
                }
                /* Not owner, wait for exclusive lock or waiting writers (writer preference) */
            } else {
                /* Not exclusively locked.
                 * If there are writers waiting (VXWANT), we must wait to prevent writer starvation.
                 */
                 if (!(vp->v_flag & VXWANT)) {
                     break;
                 }
            }

            if (flags & LK_NOWAIT) {
                spinlock_release(&vp->v_interlock);
                return -EAGAIN;
            }

            vp->v_flag |= VXWANT;
            sleepq_add(vp, current_thread);
            spinlock_release(&vp->v_interlock);
            sched_yield();
            spinlock_acquire(&vp->v_interlock);
        }

        /* Acquired shared lock */
        /* If not already locked, set LK_SHARED (1) */
        /* If already locked shared, v_lockstate increments */
        /* v_lockstate acts as reader count */
        vp->v_lockstate++;
    } else {
        /* LK_EXCLUSIVE */
        while (1) {
            /* If locked (shared or exclusive), wait */
            if (vp->v_lockstate > 0) {
                if (vp->v_lockowner == current_thread) {
                    /* Recursive exclusive lock */
                    spinlock_release(&vp->v_interlock);
                    return -EDEADLK;
                }
            } else {
                /* Not locked */
                break;
            }

            if (flags & LK_NOWAIT) {
                spinlock_release(&vp->v_interlock);
                return -EAGAIN;
            }

            vp->v_flag |= VXWANT;
            sleepq_add(vp, current_thread);
            spinlock_release(&vp->v_interlock);
            sched_yield();
            spinlock_acquire(&vp->v_interlock);
        }

        /* Acquired exclusive lock */
        vp->v_lockstate = 1; /* Using 1 to mark locked, but VXLOCK distinguishes it */
        vp->v_flag |= VXLOCK;
        vp->v_lockowner = current_thread;
    }

    spinlock_release(&vp->v_interlock);
    return 0;
}

/*
 * vn_unlock - Unlock a vnode
 */
void vn_unlock(struct vnode *vp)
{
    spinlock_acquire(&vp->v_interlock);

    if (vp->v_flag & VXLOCK) {
        /* Exclusive unlock */
        if (vp->v_lockowner != current_thread) {
             panic("vn_unlock: not owner");
        }
        vp->v_lockstate = 0;
        vp->v_flag &= ~VXLOCK;
        vp->v_lockowner = NULL;
    } else {
        /* Shared unlock */
        if (vp->v_lockstate > 0) {
            vp->v_lockstate--;
        } else {
            panic("vn_unlock: not locked");
        }
    }
    
    /* Wake up waiters if lock is now free */
    /* For shared: only wake if count dropped to 0 */
    /* For exclusive: always wake */
    if ((vp->v_lockstate == 0) && (vp->v_flag & VXWANT)) {
        sleepq_wake_one(vp);
        /* We can't clear VXWANT safely without checking if more waiters exist.
           The sleepq implementation will handle waking one.
           If we are draining, we might need to wake all or ensure propagation.
           Wake one is usually sufficient as the woken thread will wake next.
        */
        if (!sleepq_has_waiters(vp)) {
            vp->v_flag &= ~VXWANT;
        }
    }
    
    spinlock_release(&vp->v_interlock);
}

/*
 * vn_islocked - Check if vnode is locked
 */
int vn_islocked(struct vnode *vp)
{
    if (vp->v_flag & VXLOCK) return 2; /* Exclusive */
    if (vp->v_lockstate > 0) return 1; /* Shared */
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
 * vnode_reclaim - Final destruction of a vnode
 *
 * Called when vnode has no references and should be freed.
 */
void vnode_reclaim(struct vnode *vp)
{
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
    
    if (!vp->v_mount || vp->v_ino == 0) {
        return;  /* Can't cache without mount/ino */
    }
    
    hash = vnode_hash(vp->v_mount, vp->v_ino);
    
    spinlock_acquire(&vnode_hash_lock);
    vp->v_hash_next = vnode_hashtable[hash];
    vnode_hashtable[hash] = vp;
    spinlock_release(&vnode_hash_lock);
}

/*
 * vnode_cache_remove - Remove vnode from lookup cache
 */
void vnode_cache_remove(struct vnode *vp)
{
    uint32_t hash;
    struct vnode **pp;
    
    if (!vp->v_mount) {
        return;
    }
    
    hash = vnode_hash(vp->v_mount, vp->v_ino);
    
    spinlock_acquire(&vnode_hash_lock);
    for (pp = &vnode_hashtable[hash]; *pp; pp = &(*pp)->v_hash_next) {
        if (*pp == vp) {
            *pp = vp->v_hash_next;
            vp->v_hash_next = NULL;
            break;
        }
    }
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
