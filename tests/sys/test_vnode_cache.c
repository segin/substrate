#include <sys/types.h>
#include <vfs/vnode.h>
#include <kern/console.h>
#include <kern/panic.h>
#include <vm/vm_kmem.h>
#include <string.h>
#include <sys/mount.h>

void test_vnode_cache_insert(void) {
    kprint("\n--- Test: vnode_cache_insert ---\n");

    struct mount mock_mount;
    memset(&mock_mount, 0, sizeof(struct mount));

    struct vnode vp1;
    memset(&vp1, 0, sizeof(struct vnode));
    vp1.v_usecount = 1;
    vp1.v_mount = &mock_mount;
    vp1.v_ino = 200;

    vnode_cache_insert(&vp1);
    struct vnode *found = vnode_lookup_cache(&mock_mount, 200);
    if (found == &vp1) {
        kprint("PASS: Inserted valid vnode found in cache\n");
        vrele(found);
    } else {
        kprint("FAIL: Inserted valid vnode not found in cache\n");
    }

    vnode_cache_remove(&vp1);

    struct vnode vp2;
    memset(&vp2, 0, sizeof(struct vnode));
    vp2.v_usecount = 1;
    vp2.v_mount = NULL;
    vp2.v_ino = 201;

    vnode_cache_insert(&vp2);
    found = vnode_lookup_cache(NULL, 201);
    if (found == NULL) {
        kprint("PASS: Handled NULL v_mount properly (not inserted)\n");
    } else {
        kprint("FAIL: Inserted vnode with NULL v_mount\n");
        vrele(found);
    }
    vnode_cache_remove(&vp2);

    struct vnode vp3;
    memset(&vp3, 0, sizeof(struct vnode));
    vp3.v_usecount = 1;
    vp3.v_mount = &mock_mount;
    vp3.v_ino = 0;

    vnode_cache_insert(&vp3);
    found = vnode_lookup_cache(&mock_mount, 0);
    if (found == NULL) {
        kprint("PASS: Handled 0 v_ino properly (not inserted)\n");
    } else {
        kprint("FAIL: Inserted vnode with 0 v_ino\n");
        vrele(found);
    }
    vnode_cache_remove(&vp3);
}

void test_vnode_cache_remove(void) {
    kprint("\n--- Test: vnode_cache_remove ---\n");

    struct mount mock_mount;
    memset(&mock_mount, 0, sizeof(struct mount));

    struct vnode vp1;
    memset(&vp1, 0, sizeof(struct vnode));
    vp1.v_usecount = 1;
    vp1.v_mount = &mock_mount;
    vp1.v_ino = 100;

    struct vnode *found = vnode_lookup_cache(&mock_mount, 100);
    if (found != NULL) {
        kprint("FAIL: Found vp1 before insert\n");
    } else {
        kprint("PASS: Did not find vp1 before insert\n");
    }

    vnode_cache_insert(&vp1);

    found = vnode_lookup_cache(&mock_mount, 100);
    if (found == &vp1) {
        kprint("PASS: Found vp1 after insert\n");
        vrele(found);
    } else {
        kprint("FAIL: Did not find vp1 after insert\n");
    }

    vnode_cache_remove(&vp1);

    found = vnode_lookup_cache(&mock_mount, 100);
    if (found == NULL) {
        kprint("PASS: Did not find vp1 after remove\n");
    } else {
        kprint("FAIL: Found vp1 after remove\n");
        vrele(found);
    }

    struct vnode vp3;
    memset(&vp3, 0, sizeof(struct vnode));
    vnode_cache_remove(&vp3);
    kprint("PASS: vnode_cache_remove handled NULL v_mount safely\n");
}

void run_vnode_cache_tests(void) {
    kprint("=== Running Vnode Lookup Cache Tests ===\n");
    test_vnode_cache_insert();
    test_vnode_cache_remove();
    kprint("=== Vnode Lookup Cache Tests Complete ===\n");
}
