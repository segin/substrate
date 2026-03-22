#include <sys/types.h>
#include <sys/errno.h>
#include <vfs/vnode.h>
#include <sys/mount.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

bool test_vnode_cache_insert_basic(void) {
    struct mount mock_mount;
    memset(&mock_mount, 0, sizeof(struct mount));

    struct vnode vp;
    memset(&vp, 0, sizeof(struct vnode));
    vp.v_usecount = 1;
    vp.v_mount = &mock_mount;
    vp.v_ino = 300;

    vnode_cache_insert(&vp);

    struct vnode *found = vnode_lookup_cache(&mock_mount, 300);
    if (found != &vp) {
        if (found) vrele(found);
        vnode_cache_remove(&vp);
        return false;
    }

    vrele(found);
    vnode_cache_remove(&vp);
    return true;
}

bool test_vnode_cache_insert_no_mount(void) {
    struct vnode vp;
    memset(&vp, 0, sizeof(struct vnode));
    vp.v_usecount = 1;
    vp.v_mount = NULL;
    vp.v_ino = 301;

    vnode_cache_insert(&vp);

    struct vnode *found = vnode_lookup_cache(NULL, 301);
    if (found != NULL) {
        vrele(found);
        vnode_cache_remove(&vp);
        return false;
    }

    vnode_cache_remove(&vp);
    return true;
}

bool test_vnode_cache_insert_zero_ino(void) {
    struct mount mock_mount;
    memset(&mock_mount, 0, sizeof(struct mount));

    struct vnode vp;
    memset(&vp, 0, sizeof(struct vnode));
    vp.v_usecount = 1;
    vp.v_mount = &mock_mount;
    vp.v_ino = 0;

    vnode_cache_insert(&vp);

    struct vnode *found = vnode_lookup_cache(&mock_mount, 0);
    if (found != NULL) {
        vrele(found);
        vnode_cache_remove(&vp);
        return false;
    }

    vnode_cache_remove(&vp);
    return true;
}
