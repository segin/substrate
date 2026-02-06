#include <kern/console.h>
#include <sys/types.h>
#include <sys/namei.h>
#include <vfs/vnode.h>
#include <sys/errno.h>
#include <string.h>
#include "tests.h"

static int
mock_vop_create(struct vnode *dvp, struct vnode **vpp, struct componentname *cnp, struct vattr *vap)
{
    (void)dvp; (void)vpp; (void)cnp; (void)vap;
    return 0; // Success
}

static int
mock_vop_mknod(struct vnode *dvp, struct vnode **vpp, struct componentname *cnp, struct vattr *vap)
{
    (void)dvp; (void)vpp; (void)cnp; (void)vap;
    return 0; // Success
}

static int
mock_vop_mkdir(struct vnode *dvp, struct vnode **vpp, struct componentname *cnp, struct vattr *vap)
{
    (void)dvp; (void)vpp; (void)cnp; (void)vap;
    return 0; // Success
}

static int
mock_vop_remove(struct vnode *dvp, struct vnode *vp, struct componentname *cnp)
{
    (void)dvp; (void)vp; (void)cnp;
    return 0;
}

static int
mock_vop_rmdir(struct vnode *dvp, struct vnode *vp, struct componentname *cnp)
{
    (void)dvp; (void)vp; (void)cnp;
    return 0;
}

static int
mock_vop_whiteout(struct vnode *dvp, struct componentname *cnp, int flags)
{
    (void)dvp; (void)cnp; (void)flags;
    return 0;
}

static struct vnodeops mock_ops = {
    .vop_create = mock_vop_create,
    .vop_mknod = mock_vop_mknod,
    .vop_mkdir = mock_vop_mkdir,
    .vop_remove = mock_vop_remove,
    .vop_rmdir = mock_vop_rmdir,
    .vop_whiteout = mock_vop_whiteout,
};

void run_vnode_ops_tests(void) {
    kprint("TEST: Checking vnode_ops wrappers...\n");

    struct vnode mock_dir = {0};
    mock_dir.v_type = VDIR;
    mock_dir.v_op = &mock_ops;
    
    struct vnode mock_file = {0};
    mock_file.v_type = VREG; // Not a directory

    struct componentname cnp = {0};
    struct vattr vap = {0};

    // Test vop_create
    if (vop_create(&mock_dir, NULL, &cnp, &vap) == 0) {
        kprint("PASS: vop_create delegates correctly\n");
    } else {
        kprint("FAIL: vop_create failed\n");
    }

    if (vop_create(&mock_file, NULL, &cnp, &vap) == ENOTDIR) {
        kprint("PASS: vop_create checks ENOTDIR\n");
    } else {
        kprint("FAIL: vop_create allowed non-dir\n");
    }

    /* Test EOPNOTSUPP */
    struct vnode mock_noops = {0};
    mock_noops.v_type = VDIR;
    if (vop_create(&mock_noops, NULL, &cnp, &vap) == EOPNOTSUPP) {
        kprint("PASS: vop_create return EOPNOTSUPP when op missing\n");
    } else {
        kprint("FAIL: vop_create did not return EOPNOTSUPP\n");
    }

    // Test vop_mknod
    if (vop_mknod(&mock_dir, NULL, &cnp, &vap) == 0) {
        kprint("PASS: vop_mknod delegates correctly\n");
    } else {
        kprint("FAIL: vop_mknod failed\n");
    }
    
    // Test vop_mkdir
    if (vop_mkdir(&mock_dir, NULL, &cnp, &vap) == 0) {
        kprint("PASS: vop_mkdir delegates correctly\n");
    } else {
        kprint("FAIL: vop_mkdir failed\n");
    }

    // Test vop_remove
    if (vop_remove(&mock_dir, &mock_file, &cnp) == 0) {
        kprint("PASS: vop_remove delegates correctly\n");
    } else {
        kprint("FAIL: vop_remove failed\n");
    }

    // Test vop_rmdir
    if (vop_rmdir(&mock_dir, &mock_dir, &cnp) == 0) {
        kprint("PASS: vop_rmdir delegates correctly\n");
    } else {
        kprint("FAIL: vop_rmdir failed\n");
    }

    // Test vop_whiteout
    if (vop_whiteout(&mock_dir, &cnp, CREATE) == 0) {
        kprint("PASS: vop_whiteout delegates correctly\n");
    } else {
        kprint("FAIL: vop_whiteout failed\n");
    }
}
