#include <sys/types.h>
#include <vfs/vnode.h>
#include <kern/console.h>
#include <kern/panic.h>
#include <vm/vm_kmem.h>
#include <string.h>
#include <sys/mount.h>

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
    test_vnode_cache_remove();
    kprint("=== Vnode Lookup Cache Tests Complete ===\n");
}
