#include <kern/console.h>
#include <sys/types.h>
#include <vfs/vnode.h>
#include <sys/errno.h>
#include <string.h>
#include "tests.h"

static int
mock_vop_create(struct vnode *dvp, struct vnode **vpp, const char *name, mode_t mode, struct ucred *cred)
{
    (void)dvp; (void)vpp; (void)name; (void)mode; (void)cred;
    return 0; // Success
}

static int
mock_vop_mknod(struct vnode *dvp, struct vnode **vpp, const char *name, mode_t mode, dev_t dev, struct ucred *cred)
{
    (void)dvp; (void)vpp; (void)name; (void)mode; (void)dev; (void)cred;
    return 0; // Success
}

static int
mock_vop_mkdir(struct vnode *dvp, struct vnode **vpp, const char *name, mode_t mode, struct ucred *cred)
{
    (void)dvp; (void)vpp; (void)name; (void)mode; (void)cred;
    return 0; // Success
}

static struct vnodeops mock_ops = {
    .vop_create = mock_vop_create,
    .vop_mknod = mock_vop_mknod,
    .vop_mkdir = mock_vop_mkdir,
};

void run_vnode_ops_tests(void) {
    kprint("TEST: Checking vnode_ops wrappers...\n");

    struct vnode mock_dir = {0};
    mock_dir.v_type = VDIR;
    mock_dir.v_op = &mock_ops;
    
    struct vnode mock_file = {0};
    mock_file.v_type = VREG; // Not a directory

    // Test vop_create
    if (vop_create(&mock_dir, NULL, "foo", 0644, NULL) == 0) {
        kprint("PASS: vop_create delegates correctly\n");
    } else {
        kprint("FAIL: vop_create failed\n");
    }

    if (vop_create(&mock_file, NULL, "foo", 0644, NULL) == ENOTDIR) {
        kprint("PASS: vop_create checks ENOTDIR\n");
    } else {
        kprint("FAIL: vop_create allowed non-dir\n");
    }

    /* Test EOPNOTSUPP */
    struct vnode mock_noops = {0};
    mock_noops.v_type = VDIR;
    // v_op is NULL or has NULL function pointers
    if (vop_create(&mock_noops, NULL, "foo", 0644, NULL) == EOPNOTSUPP) {
        kprint("PASS: vop_create return EOPNOTSUPP when op missing\n");
    } else {
        kprint("FAIL: vop_create did not return EOPNOTSUPP\n");
    }


    // Test vop_mknod
    if (vop_mknod(&mock_dir, NULL, "foo", 0644, 0, NULL) == 0) {
        kprint("PASS: vop_mknod delegates correctly\n");
    } else {
        kprint("FAIL: vop_mknod failed\n");
    }
    
    if (vop_mknod(&mock_file, NULL, "foo", 0644, 0, NULL) == ENOTDIR) {
        kprint("PASS: vop_mknod checks ENOTDIR\n");
    } else {
        kprint("FAIL: vop_mknod allowed non-dir\n");
    }

    // Test vop_mkdir
    if (vop_mkdir(&mock_dir, NULL, "foo", 0755, NULL) == 0) {
        kprint("PASS: vop_mkdir delegates correctly\n");
    } else {
        kprint("FAIL: vop_mkdir failed\n");
    }
    
    if (vop_mkdir(&mock_file, NULL, "foo", 0755, NULL) == ENOTDIR) {
        kprint("PASS: vop_mkdir checks ENOTDIR\n");
    } else {
        kprint("FAIL: vop_mkdir allowed non-dir\n");
    }
}
