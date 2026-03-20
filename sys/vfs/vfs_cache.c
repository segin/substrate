#include <sys/types.h>
#include <sys/namei.h>
#include <vfs/vnode.h>
#include <sys/mount.h>
#include <sys/queue.h>
#include <sys/errno.h>
#include <vm/vm_kmem.h>
#include <kern/panic.h>
#include <sys/lock.h>
#include <string.h>

/*
 * Name Cache structure
 */
struct namecache {
    LIST_ENTRY(namecache) nc_hash;  /* Hash chain */
    TAILQ_ENTRY(namecache) nc_lru;  /* LRU chain */
    struct vnode *nc_dvp;           /* Parent directory vnode */
    struct vnode *nc_vp;            /* Target vnode */
    size_t nc_nlen;                 /* Length of name */
    char nc_name[0];                /* Name (inline) */
};

#define NCHASH_SIZE 1024
#define NCHASH_MASK (NCHASH_SIZE - 1)

static LIST_HEAD(nchash_head, namecache) nchash[NCHASH_SIZE];
static TAILQ_HEAD(nclru_head, namecache) nclru;

/* Global cache limits */
int vfs_cache_limit = 10240;
int vfs_cache_count = 0;

/* Reader/writer lock for SMP scalability */
static rwlock_t nchash_lock;

/* Simple hash function for name cache */
static uint32_t
cache_hash(struct vnode *dvp, const char *name, size_t len)
{
    uint32_t hash = (uint32_t)(uintptr_t)dvp;
    for (size_t i = 0; i < len; i++) {
        hash = (hash << 5) + hash + name[i];
    }
    return hash & NCHASH_MASK;
}

/*
 * cache_lookup:
 * Search the name cache for a name in a directory.
 */
int
cache_lookup(struct vnode *dvp, struct vnode **vpp, const char *name, size_t len)
{
    uint32_t hash = cache_hash(dvp, name, len);
    struct namecache *ncp;

    rw_wlock(&nchash_lock);

    LIST_FOREACH(ncp, &nchash[hash], nc_hash) {
        if (ncp->nc_dvp == dvp && ncp->nc_nlen == len &&
            memcmp(ncp->nc_name, name, len) == 0) {
            
            /* Cache hit */
            if (ncp->nc_vp == NULL) {
                /* Negative cache entry: name does not exist */
                *vpp = NULL;
                TAILQ_REMOVE(&nclru, ncp, nc_lru);
                TAILQ_INSERT_TAIL(&nclru, ncp, nc_lru);
                rw_wunlock(&nchash_lock);
                return ENOENT;
            }
            *vpp = ncp->nc_vp;
            vref(*vpp);

            /* Move to tail of LRU (most recently used) */
            TAILQ_REMOVE(&nclru, ncp, nc_lru);
            TAILQ_INSERT_TAIL(&nclru, ncp, nc_lru);
            rw_wunlock(&nchash_lock);
            return 0;
        }
    }

    rw_wunlock(&nchash_lock);
    return ENOENT;
}

/*
 * cache_enter:
 * Enter a directory entry into the name cache.
 */
void
cache_enter(struct vnode *dvp, struct vnode *vp, const char *name, size_t len)
{
    if (len > 255) return; /* Limit name length in cache */

    uint32_t hash = cache_hash(dvp, name, len);
    struct namecache *ncp;

    rw_wlock(&nchash_lock);

    /* Check if already in cache */
    LIST_FOREACH(ncp, &nchash[hash], nc_hash) {
        if (ncp->nc_dvp == dvp && ncp->nc_nlen == len &&
            memcmp(ncp->nc_name, name, len) == 0) {
            
            /* Already cached, update vp if changed */
            if (ncp->nc_vp != vp) {
                ncp->nc_vp = vp;
            }
            rw_wunlock(&nchash_lock);
            return;
        }
    }

    /* Allocate new cache entry */
    ncp = kmalloc(sizeof(struct namecache) + len + 1);
    if (ncp == NULL) {
        rw_wunlock(&nchash_lock);
        return;
    }

    /* Enforce cache limit */
    if (vfs_cache_count >= vfs_cache_limit) {
        struct namecache *lru = TAILQ_FIRST(&nclru);
        if (lru) {
            LIST_REMOVE(lru, nc_hash);
            TAILQ_REMOVE(&nclru, lru, nc_lru);
            kfree(lru, sizeof(struct namecache) + lru->nc_nlen + 1);
            vfs_cache_count--;
        }
    }

    ncp->nc_dvp = dvp;
    ncp->nc_vp = vp;
    ncp->nc_nlen = len;
    memcpy(ncp->nc_name, name, len);
    ncp->nc_name[len] = '\0';

    LIST_INSERT_HEAD(&nchash[hash], ncp, nc_hash);
    TAILQ_INSERT_TAIL(&nclru, ncp, nc_lru);
    vfs_cache_count++;

    rw_wunlock(&nchash_lock);
}

/*
 * cache_purge:
 * Purge all cache entries for a vnode.
 */
void
cache_purge(struct vnode *vp)
{
    struct namecache *ncp, *tncp;

    rw_wlock(&nchash_lock);

    for (int i = 0; i < NCHASH_SIZE; i++) {
        LIST_FOREACH_SAFE(ncp, &nchash[i], nc_hash, tncp) {
            if (ncp->nc_dvp == vp || ncp->nc_vp == vp) {
                LIST_REMOVE(ncp, nc_hash);
                TAILQ_REMOVE(&nclru, ncp, nc_lru);
                kfree(ncp, sizeof(struct namecache) + ncp->nc_nlen + 1);
                vfs_cache_count--;
            }
        }
    }

    rw_wunlock(&nchash_lock);
}

/*
 * Initialise name cache.
 */
void
nchinit(void)
{
    for (int i = 0; i < NCHASH_SIZE; i++) {
        LIST_INIT(&nchash[i]);
    }
    TAILQ_INIT(&nclru);
    rwlock_init(&nchash_lock, "nchash");
}

/*
 * cache_purgevfs:
 * Purge all cache entries for a mount point.
 */
void
cache_purgevfs(struct mount *mp)
{
    struct namecache *ncp, *tncp;

    rw_wlock(&nchash_lock);

    for(int i = 0; i < NCHASH_SIZE; i++) {
        LIST_FOREACH_SAFE(ncp, &nchash[i], nc_hash, tncp) {
            if(ncp->nc_dvp && ncp->nc_dvp->v_mount == mp) {
                LIST_REMOVE(ncp, nc_hash);
                TAILQ_REMOVE(&nclru, ncp, nc_lru);
                kfree(ncp, sizeof(struct namecache) + ncp->nc_nlen + 1);
                vfs_cache_count--;
            }
        }
    }

    rw_wunlock(&nchash_lock);
}
