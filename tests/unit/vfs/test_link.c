#include <sys/types.h>
#include <vfs/vnode.h>
#include <sys/errno.h>
#include <sys/namei.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>

static int mock_link_called = 0;
static struct vnode *mock_tdvp_arg = NULL;
static struct vnode *mock_vp_arg = NULL;
static const char *mock_name_arg = NULL;

static int
mock_vop_link(struct vnode *tdvp, struct vnode *vp, struct componentname *cnp)
{
    mock_link_called++;
    mock_tdvp_arg = tdvp;
    mock_vp_arg = vp;
    mock_name_arg = cnp->cn_nameptr;
    return 0;
}

static struct vnodeops mock_vops_link = {
    .vop_link = mock_vop_link,
};

static struct vnodeops mock_vops_nolink = {
    .vop_link = NULL,
};

bool test_vop_link_basic(void) {
    struct vnode tdvp, vp;
    struct componentname cnp;
    int error;

    memset(&tdvp, 0, sizeof(tdvp));
    memset(&vp, 0, sizeof(vp));
    tdvp.v_type = VDIR;
    tdvp.v_op = &mock_vops_link;
    vp.v_type = VREG;

    cnp.cn_nameptr = "linkname";
    cnp.cn_namelen = 8;

    mock_link_called = 0;
    mock_tdvp_arg = NULL;
    mock_vp_arg = NULL;
    mock_name_arg = NULL;

    error = vop_link(&tdvp, &vp, &cnp);

    if (error != 0) return false;
    if (mock_link_called != 1) return false;
    if (mock_tdvp_arg != &tdvp) return false;
    if (mock_vp_arg != &vp) return false;
    if (strcmp(mock_name_arg, "linkname") != 0) return false;

    return true;
}

bool test_vop_link_notdir(void) {
    struct vnode tdvp, vp;
    struct componentname cnp;
    int error;

    memset(&tdvp, 0, sizeof(tdvp));
    tdvp.v_type = VREG; // Not a directory
    vp.v_type = VREG;

    error = vop_link(&tdvp, &vp, &cnp);

    if (error != ENOTDIR) return false;

    return true;
}

bool test_vop_link_dir_target(void) {
    struct vnode tdvp, vp;
    struct componentname cnp;
    int error;

    memset(&tdvp, 0, sizeof(tdvp));
    memset(&vp, 0, sizeof(vp));
    tdvp.v_type = VDIR;
    vp.v_type = VDIR; // Cannot link directory

    error = vop_link(&tdvp, &vp, &cnp);

    if (error != EPERM) return false;

    return true;
}

bool test_vop_link_notsupp(void) {
    struct vnode tdvp, vp;
    struct componentname cnp;
    int error;

    memset(&tdvp, 0, sizeof(tdvp));
    memset(&vp, 0, sizeof(vp));
    tdvp.v_type = VDIR;
    tdvp.v_op = &mock_vops_nolink;
    vp.v_type = VREG;

    error = vop_link(&tdvp, &vp, &cnp);

    if (error != EOPNOTSUPP) return false;

    return true;
}
