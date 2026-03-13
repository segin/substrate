#include <sys/types.h>
#include <sys/namei.h>
#include <vfs/vnode.h>
#include <sys/errno.h>
#include <kern/console.h>
#include <kern/panic.h>
#include <vm/vm_kmem.h>
#include <string.h>
#include <sys/mount.h>

/* Externs from vfs_cache.c */
extern int vfs_cache_limit;
extern int vfs_cache_count;

int snprintf(char *str, size_t size, const char *format, ...);

/* Mock vnode initialization */
static struct vnode *
create_mock_vnode(void)
{
    struct vnode *vp = kmalloc(sizeof(struct vnode));
    if (!vp) panic("create_mock_vnode: out of memory");
    memset(vp, 0, sizeof(*vp));
    spinlock_init(&vp->v_interlock, "mock_vnode");
    vp->v_usecount = 1; /* Start with one reference */
    return vp;
}

static void
free_mock_vnode(struct vnode *vp)
{
    kfree(vp, sizeof(struct vnode));
}

static int
test_vfs_cache_limit(void)
{
    struct vnode *dvp = create_mock_vnode();
    struct vnode *vp = create_mock_vnode();
    struct vnode *lookup_vp;
    int initial_limit = vfs_cache_limit;
    int i;
    char name[32];
    int failures = 0;

    kprint("  Testing VFS cache limit...\n");

    /* Set a small limit for testing */
    vfs_cache_limit = 10;

    /* Clear any existing entries by purging dvp?
       Ideally we want a clean slate, but we can't easily clear the global cache without purging everything.
       Since we use a unique dvp, entries for other dvps shouldn't interfere with our lookup,
       but they contribute to the global count.
       So we need to account for existing entries.
    */

    int start_count = vfs_cache_count;

    /* Fill the cache up to the limit */
    for (i = 0; i < 10; i++) {
        snprintf(name, sizeof(name), "file%d", i);
        cache_enter(dvp, vp, name, strlen(name));
    }

    if (vfs_cache_count != start_count + 10) {
        kprintf("FAILURE: Cache count expected %d, got %d\n", start_count + 10, vfs_cache_count);
        failures++;
    } else {
        kprint("PASS: Cache filled to limit\n");
    }

    /* Add one more, should trigger eviction */
    snprintf(name, sizeof(name), "file10");
    cache_enter(dvp, vp, name, strlen(name));

    if (vfs_cache_count != start_count + 10) {
        kprintf("FAILURE: Cache count grew beyond limit: %d (expected %d)\n", vfs_cache_count, start_count + 10);
        failures++;
    } else {
        kprint("PASS: Cache count respected limit\n");
    }

    /* Verify the oldest entry ("file0") is gone */
    if (cache_lookup(dvp, &lookup_vp, "file0", 5) == 0) {
        kprint("FAILURE: Oldest entry 'file0' was not evicted\n");
        vrele(lookup_vp); /* Release ref from lookup */
        failures++;
    } else {
        kprint("PASS: Oldest entry evicted\n");
    }

    /* Verify "file1" is still there */
    if (cache_lookup(dvp, &lookup_vp, "file1", 5) == 0) {
        vrele(lookup_vp);
        kprint("PASS: 'file1' preserved\n");
    } else {
        kprint("FAILURE: 'file1' missing\n");
        failures++;
    }

    /* Verify "file10" is present */
    if (cache_lookup(dvp, &lookup_vp, "file10", 6) == 0) {
        vrele(lookup_vp);
        kprint("PASS: 'file10' present\n");
    } else {
        kprint("FAILURE: 'file10' missing\n");
        failures++;
    }

    /* Cleanup */
    cache_purge(dvp);
    free_mock_vnode(dvp);
    free_mock_vnode(vp);

    /* Restore limit */
    vfs_cache_limit = initial_limit;

    return failures;
}

static int
test_vfs_cache_lru(void)
{
    struct vnode *dvp = create_mock_vnode();
    struct vnode *vp = create_mock_vnode();
    struct vnode *lookup_vp;
    int initial_limit = vfs_cache_limit;
    int i;
    char name[32];
    int failures = 0;

    kprint("  Testing VFS cache LRU behavior...\n");

    vfs_cache_limit = 5;

    /* Fill with 5 entries: file0..file4 */
    for (i = 0; i < 5; i++) {
        snprintf(name, sizeof(name), "file%d", i);
        cache_enter(dvp, vp, name, strlen(name));
    }

    /* Access "file0" to make it MRU */
    if (cache_lookup(dvp, &lookup_vp, "file0", 5) == 0) {
        vrele(lookup_vp);
    } else {
        kprint("FAILURE: Could not lookup 'file0'\n");
        failures++;
    }

    /* Add "file5", should evict LRU.
       "file0" was accessed, so it should be MRU.
       "file1" should be the LRU now.
    */
    cache_enter(dvp, vp, "file5", 5);

    /* Verify "file1" is gone */
    if (cache_lookup(dvp, &lookup_vp, "file1", 5) == 0) {
        kprint("FAILURE: 'file1' (LRU) was not evicted\n");
        vrele(lookup_vp);
        failures++;
    } else {
        kprint("PASS: 'file1' (LRU) evicted\n");
    }

    /* Verify "file0" is still there */
    if (cache_lookup(dvp, &lookup_vp, "file0", 5) == 0) {
        vrele(lookup_vp);
        kprint("PASS: 'file0' (MRU) preserved\n");
    } else {
        kprint("FAILURE: 'file0' (MRU) was evicted prematurely\n");
        failures++;
    }

    /* Cleanup */
    cache_purge(dvp);
    free_mock_vnode(dvp);
    free_mock_vnode(vp);
    vfs_cache_limit = initial_limit;

    return failures;
}

static int
test_vnode_cache_insert_lookup(void)
{
    int failures = 0;
    struct vnode *vp;
    struct mount mock_mount;
    struct vnode *lookup_vp;

    kprint("  Testing vnode_cache_insert and vnode_lookup_cache...\n");

    /* Initialize a vnode */
    vp = create_mock_vnode();
    memset(&mock_mount, 0, sizeof(mock_mount));
    vp->v_mount = &mock_mount;
    vp->v_ino = 12345;

    /* Should not be found initially */
    lookup_vp = vnode_lookup_cache(&mock_mount, 12345);
    if (lookup_vp != NULL) {
        kprint("FAILURE: vnode_lookup_cache found non-existent vnode\n");
        vrele(lookup_vp);
        failures++;
    }

    /* Insert into cache */
    vnode_cache_insert(vp);

    /* Look it up */
    lookup_vp = vnode_lookup_cache(&mock_mount, 12345);

    if (lookup_vp != vp) {
        kprint("FAILURE: vnode_lookup_cache failed to find inserted vnode\n");
        if (lookup_vp) vrele(lookup_vp);
        failures++;
    } else {
        kprint("PASS: vnode_lookup_cache found inserted vnode\n");
        vrele(lookup_vp); /* vnode_lookup_cache vref()s, vrele will drop our extra ref */
    }

    /* Remove from cache */
    vnode_cache_remove(vp);

    /* Ensure it's removed */
    lookup_vp = vnode_lookup_cache(&mock_mount, 12345);
    if (lookup_vp != NULL) {
        kprint("FAILURE: vnode_lookup_cache found vnode after removal\n");
        failures++;
        vrele(lookup_vp);
    } else {
        kprint("PASS: vnode_lookup_cache correctly returned NULL after removal\n");
    }

    /* Test invalid vnode (no mount) */
    vp->v_mount = NULL;
    vnode_cache_insert(vp); /* Should be a no-op */

    /* Look up with original mount, shouldn't find anything */
    lookup_vp = vnode_lookup_cache(&mock_mount, 12345);
    if (lookup_vp != NULL) {
        kprint("FAILURE: vnode_cache_insert inserted vnode without mount\n");
        failures++;
        vrele(lookup_vp);
    }

    free_mock_vnode(vp);

    return failures;
}

void
run_vfs_cache_tests(void)
{
    int failures = 0;

    kprint("=== Running VFS Cache Tests ===\n");

    failures += test_vnode_cache_insert_lookup();
    failures += test_vfs_cache_limit();
    failures += test_vfs_cache_lru();

    if (failures == 0) {
        kprint("VFS Cache Tests: PASS\n");
    } else {
        kprintf("VFS Cache Tests: FAIL (%d failures)\n", failures);
    }
}
