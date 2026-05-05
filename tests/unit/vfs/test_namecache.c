/*
/*
 * test_namecache.c - Unit tests for the VFS name cache (nchash)
 *
 * Covers: cache_lookup hit/miss, cache_enter, cache_purge, negative entries
 * (REQ-04-0188)
 *
 * cache_lookup return values:
 *   0       - cache hit, *vpp set (also increments usecount via vref)
 *   ENOENT  - miss OR negative entry, *vpp unchanged
 */
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <vfs/vnode.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

/* Helper: allocate a vnode of the given type */
static struct vnode *nc_alloc_vnode(const char *tag, enum vtype type)
{
    struct vnode *vp = NULL;
    if (getnewvnode(tag, NULL, NULL, &vp) != 0 || !vp)
        return NULL;
    vp->v_type = type;
    return vp;
}

/* Helper: release a vnode safely */
static void nc_free_vnode(struct vnode *vp)
{
    if (!vp) return;
    vp->v_usecount = 0;
    vp->v_flag &= ~VONFREELIST;
    vnode_reclaim(vp);
}

/* --- cache_enter / cache_lookup hit --------------------------------------- */

bool test_namecache_hit(void) {
    vnode_init();
    nchinit();

    struct vnode *dvp = nc_alloc_vnode("nc_hit_dir", VDIR);
    struct vnode *vp  = nc_alloc_vnode("nc_hit_child", VREG);
    if (!dvp || !vp) {
        nc_free_vnode(dvp);
        nc_free_vnode(vp);
        return false;
    }

    const char *name = "testfile";
    size_t namelen = strlen(name);

    cache_enter(dvp, vp, name, namelen);

    struct vnode *found = NULL;
    int result = cache_lookup(dvp, &found, name, namelen);

    /* Hit: result == 0, found == vp; cache_lookup adds a vref */
    bool ok = (result == 0 && found == vp);

    cache_purge(dvp);
    cache_purge(vp);
    /* cache_lookup called vref() on the found vnode, drop it */
    if (ok) vrele(found);

    nc_free_vnode(dvp);
    nc_free_vnode(vp);
    return ok;
}

/* --- cache_lookup miss ---------------------------------------------------- */

bool test_namecache_miss(void) {
    vnode_init();
    nchinit();

    struct vnode *dvp = nc_alloc_vnode("nc_miss_dir", VDIR);
    if (!dvp) return false;

    /* Do NOT cache any entry */
    struct vnode *found = NULL;
    int result = cache_lookup(dvp, &found, "nosuchname", 10);

    /* Miss returns ENOENT */
    bool ok = (result == ENOENT && found == NULL);

    nc_free_vnode(dvp);
    return ok;
}

/* --- negative cache entry ------------------------------------------------- */

bool test_namecache_negative(void) {
    vnode_init();
    nchinit();

    struct vnode *dvp = nc_alloc_vnode("nc_neg_dir", VDIR);
    if (!dvp) return false;

    const char *name = "doesnotexist";
    size_t namelen = strlen(name);

    /* Enter a negative entry (vp == NULL) */
    cache_enter(dvp, NULL, name, namelen);

    struct vnode *found = NULL;
    int result = cache_lookup(dvp, &found, name, namelen);

    /* Negative hit: returns ENOENT, found unchanged (NULL) */
    bool ok = (result == ENOENT && found == NULL);

    cache_purge(dvp);
    nc_free_vnode(dvp);
    return ok;
}

/* --- cache_purge removes entries ------------------------------------------ */

bool test_namecache_purge(void) {
    vnode_init();
    nchinit();

    struct vnode *dvp = nc_alloc_vnode("nc_purge_dir", VDIR);
    struct vnode *vp  = nc_alloc_vnode("nc_purge_child", VREG);
    if (!dvp || !vp) {
        nc_free_vnode(dvp);
        nc_free_vnode(vp);
        return false;
    }

    const char *name = "purgeme";
    size_t namelen = strlen(name);

    cache_enter(dvp, vp, name, namelen);

    /* Verify hit */
    struct vnode *found = NULL;
    int r1 = cache_lookup(dvp, &found, name, namelen);
    if (r1 != 0 || found != vp) {
        cache_purge(dvp);
        cache_purge(vp);
        nc_free_vnode(dvp);
        nc_free_vnode(vp);
        return false;
    }
    /* Drop the vref added by cache_lookup */
    vrele(found);

    /* Purge vp's entries and verify miss */
    cache_purge(vp);
    found = NULL;
    int r2 = cache_lookup(dvp, &found, name, namelen);

    bool ok = (r2 == ENOENT && found == NULL);

    cache_purge(dvp);
    nc_free_vnode(dvp);
    nc_free_vnode(vp);
    return ok;
}

/* --- multiple entries in same directory ----------------------------------- */

bool test_namecache_multiple_entries(void) {
    vnode_init();
    nchinit();

    struct vnode *dvp = nc_alloc_vnode("nc_multi_dir", VDIR);
    struct vnode *vp1 = nc_alloc_vnode("nc_multi_c1", VREG);
    struct vnode *vp2 = nc_alloc_vnode("nc_multi_c2", VREG);
    if (!dvp || !vp1 || !vp2) {
        nc_free_vnode(dvp);
        nc_free_vnode(vp1);
        nc_free_vnode(vp2);
        return false;
    }

    cache_enter(dvp, vp1, "alpha", 5);
    cache_enter(dvp, vp2, "beta", 4);

    struct vnode *fa = NULL, *fb = NULL;
    int ra = cache_lookup(dvp, &fa, "alpha", 5);
    int rb = cache_lookup(dvp, &fb, "beta", 4);

    bool ok = (ra == 0 && fa == vp1 && rb == 0 && fb == vp2);

    /* Drop vrefs added by cache_lookup */
    if (fa) vrele(fa);
    if (fb) vrele(fb);

    cache_purge(dvp);
    cache_purge(vp1);
    cache_purge(vp2);
    nc_free_vnode(dvp);
    nc_free_vnode(vp1);
    nc_free_vnode(vp2);
    return ok;
}

/* --- cache_purgevfs removes all entries for a mount point ----------------- */

bool test_namecache_purgevfs(void) {
    vnode_init();
    nchinit();

    struct mount mock_mp;
    memset(&mock_mp, 0, sizeof(mock_mp));

    struct vnode *dvp = nc_alloc_vnode("nc_pvfs_dir", VDIR);
    struct vnode *vp  = nc_alloc_vnode("nc_pvfs_child", VREG);
    if (!dvp || !vp) {
        nc_free_vnode(dvp);
        nc_free_vnode(vp);
        return false;
    }
    dvp->v_mount = &mock_mp;
    vp->v_mount  = &mock_mp;

    cache_enter(dvp, vp, "mounted_file", 12);

    /* Verify entry is cached */
    struct vnode *found = NULL;
    int r1 = cache_lookup(dvp, &found, "mounted_file", 12);
    if (r1 != 0) {
        cache_purge(dvp);
        cache_purge(vp);
        nc_free_vnode(dvp);
        nc_free_vnode(vp);
        return false;
    }
    vrele(found);

    /* Purge all entries for this mount */
    cache_purgevfs(&mock_mp);

    found = NULL;
    int r2 = cache_lookup(dvp, &found, "mounted_file", 12);

    bool ok = (r2 == ENOENT && found == NULL);

    nc_free_vnode(dvp);
    nc_free_vnode(vp);
    return ok;
}
