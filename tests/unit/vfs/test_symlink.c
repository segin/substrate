#include <sys/types.h>
#include <vfs/vnode.h>
#include <sys/errno.h>
#include <sys/namei.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>

static int mock_symlink_called = 0;
static struct vnode *mock_dvp_arg = NULL;
static const char *mock_name_arg = NULL;
static const char *mock_target_arg = NULL;

static int
mock_vop_symlink(struct vnode *dvp, struct vnode **vpp, struct componentname *cnp,
                 struct vattr *vap, const char *target)
{
    mock_symlink_called++;
    mock_dvp_arg = dvp;
    mock_name_arg = cnp->cn_nameptr;
    mock_target_arg = target;
    
    if (vpp) *vpp = (struct vnode *)0xDEADBEEF; // Simulate return
    (void)vap;
    return 0;
}

static struct vnodeops mock_vops_symlink = {
    .vop_symlink = mock_vop_symlink,
};

static struct vnodeops mock_vops_nosymlink = {
    .vop_symlink = NULL,
};

bool test_vop_symlink_basic(void) {
    struct vnode dvp;
    struct vnode *vpp = NULL;
    struct componentname cnp;
    struct vattr vap;
    int error;

    memset(&dvp, 0, sizeof(dvp));
    dvp.v_type = VDIR;
    dvp.v_op = &mock_vops_symlink;

    cnp.cn_nameptr = "symlink_name";
    cnp.cn_namelen = 12;
    
    const char *target = "/path/to/target";

    mock_symlink_called = 0;
    mock_dvp_arg = NULL;
    mock_name_arg = NULL;
    mock_target_arg = NULL;

    error = vop_symlink(&dvp, &vpp, &cnp, &vap, target);

    if (error != 0) return false;
    if (mock_symlink_called != 1) return false;
    if (mock_dvp_arg != &dvp) return false;
    if (strcmp(mock_name_arg, "symlink_name") != 0) return false;
    if (strcmp(mock_target_arg, target) != 0) return false;

    return true;
}

bool test_vop_symlink_notdir(void) {
    struct vnode dvp;
    struct vnode *vpp = NULL;
    struct componentname cnp;
    struct vattr vap;
    int error;

    memset(&dvp, 0, sizeof(dvp));
    dvp.v_type = VREG; // Not a directory

    error = vop_symlink(&dvp, &vpp, &cnp, &vap, "target");

    if (error != ENOTDIR) return false;

    return true;
}

bool test_vop_symlink_notsupp(void) {
    struct vnode dvp;
    struct vnode *vpp = NULL;
    struct componentname cnp;
    struct vattr vap;
    int error;

    memset(&dvp, 0, sizeof(dvp));
    dvp.v_type = VDIR;
    dvp.v_op = &mock_vops_nosymlink;

    error = vop_symlink(&dvp, &vpp, &cnp, &vap, "target");

    if (error != EOPNOTSUPP) return false;

    return true;
}
