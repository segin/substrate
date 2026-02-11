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

static int
mock_vop_getattr(struct vnode *vp, struct vattr *vap, struct ucred *cred)
{
    (void)vp; (void)cred;
    vap->va_type = vp->v_type;
    vap->va_uid = 1000;
    vap->va_gid = 1000;
    vap->va_mode = 0644;
    vap->va_size = 1234;
    return 0;
}

static int
mock_vop_setattr(struct vnode *vp, struct vattr *vap, struct ucred *cred)
{
    (void)vp; (void)vap; (void)cred;
    return 0;
}

static int
mock_vop_pathconf(struct vnode *vp, int name, register_t *retval)
{
    (void)vp; (void)name;
    *retval = 64;
    return 0;
}

static struct vnodeops mock_ops = {
    .vop_create = mock_vop_create,
    .vop_mknod = mock_vop_mknod,
    .vop_mkdir = mock_vop_mkdir,
    .vop_remove = mock_vop_remove,
    .vop_rmdir = mock_vop_rmdir,
    .vop_whiteout = mock_vop_whiteout,
    .vop_getattr = mock_vop_getattr,
    .vop_setattr = mock_vop_setattr,
    .vop_pathconf = mock_vop_pathconf,
};

void run_vnode_ops_tests(void) {
    kprint("TEST: Checking vnode_ops wrappers...\n");

    struct ucred cred = {0};
    cred.cr_uid = 1000;
    cred.cr_gid = 1000;

    struct vnode mock_dir = {0};
    mock_dir.v_type = VDIR;
    mock_dir.v_op = &mock_ops;
    
    struct vnode mock_file = {0};
    mock_file.v_type = VREG;
    mock_file.v_op = &mock_ops;

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

    // Test vop_getattr
    memset(&vap, 0, sizeof(vap));
    if (vop_getattr(&mock_file, &vap, &cred) == 0 && vap.va_size == 1234) {
        kprint("PASS: vop_getattr delegates correctly\n");
    } else {
        kprint("FAIL: vop_getattr failed\n");
    }

    // Test vop_setattr
    if (vop_setattr(&mock_file, &vap, &cred) == 0) {
        kprint("PASS: vop_setattr delegates correctly\n");
    } else {
        kprint("FAIL: vop_setattr failed\n");
    }

    // Test vop_pathconf
    register_t pc_val = 0;
    if (vop_pathconf(&mock_file, 0, &pc_val) == 0 && pc_val == 64) {
        kprint("PASS: vop_pathconf delegates correctly\n");
    } else {
        kprint("FAIL: vop_pathconf failed\n");
    }

    // Test vop_access (Generic Fallback)
    // Owner access (R_OK = 4)
    if (vop_access(&mock_file, 4, &cred) == 0) {
        kprint("PASS: vop_access (owner) allowed read\n");
    } else {
        kprint("FAIL: vop_access (owner) denied read\n");
    }

    // Root access
    struct ucred root_cred = {0};
    root_cred.cr_uid = 0;
    if (vop_access(&mock_file, 7, &root_cred) == 0) {
        kprint("PASS: vop_access (root) allowed everything\n");
    } else {
        kprint("FAIL: vop_access (root) denied access\n");
    }

    // Access Denied
    struct ucred other_cred = {0};
    other_cred.cr_uid = 2000;
    other_cred.cr_gid = 2000;
    if (vop_access(&mock_file, 2, &other_cred) != 0) {
        kprint("PASS: vop_access (other) denied write\n");
    } else {
        kprint("FAIL: vop_access (other) allowed write unexpectedly\n");
    }
}
