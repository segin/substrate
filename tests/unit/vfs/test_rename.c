#include <sys/types.h>
#include <vfs/vnode.h>
#include <sys/errno.h>
#include <sys/namei.h>
#include <sys/mount.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>

static int mock_rename_called = 0;
static struct vnode *mock_fdvp_arg = NULL;
static struct vnode *mock_fvp_arg = NULL;
static struct vnode *mock_tdvp_arg = NULL;
static struct vnode *mock_tvp_arg = NULL;

static int
mock_vop_rename(struct vnode *fdvp, struct vnode *fvp, struct componentname *fcnp,
                struct vnode *tdvp, struct vnode *tvp, struct componentname *tcnp)
{
    mock_rename_called++;
    mock_fdvp_arg = fdvp;
    mock_fvp_arg = fvp;
    mock_tdvp_arg = tdvp;
    mock_tvp_arg = tvp;
    (void)fcnp;
    (void)tcnp;
    return 0;
}

static struct vnodeops mock_vops_rename = {
    .vop_rename = mock_vop_rename,
};

static struct vnodeops mock_vops_norename = {
    .vop_rename = NULL,
};

bool test_vop_rename_basic(void) {
    struct vnode fdvp, fvp, tdvp, tvp;
    struct componentname fcnp, tcnp;
    int error;

    memset(&fdvp, 0, sizeof(fdvp));
    memset(&fvp, 0, sizeof(fvp));
    memset(&tdvp, 0, sizeof(tdvp));
    memset(&tvp, 0, sizeof(tvp));

    fdvp.v_type = VDIR;
    fdvp.v_op = &mock_vops_rename;
    fvp.v_type = VREG;
    tdvp.v_type = VDIR;
    tvp.v_type = VREG; // Target exists

    mock_rename_called = 0;
    mock_fdvp_arg = NULL;
    mock_fvp_arg = NULL;
    mock_tdvp_arg = NULL;
    mock_tvp_arg = NULL;

    error = vop_rename(&fdvp, &fvp, &fcnp, &tdvp, &tvp, &tcnp);

    if (error != 0) return false;
    if (mock_rename_called != 1) return false;
    if (mock_fdvp_arg != &fdvp) return false;
    if (mock_fvp_arg != &fvp) return false;
    if (mock_tdvp_arg != &tdvp) return false;
    if (mock_tvp_arg != &tvp) return false;

    return true;
}

bool test_vop_rename_notsupp(void) {
    struct vnode fdvp, fvp, tdvp, tvp;
    struct componentname fcnp, tcnp;
    int error;

    memset(&fdvp, 0, sizeof(fdvp));
    memset(&fvp, 0, sizeof(fvp));
    memset(&tdvp, 0, sizeof(tdvp));
    memset(&tvp, 0, sizeof(tvp));

    fdvp.v_type = VDIR;
    fdvp.v_op = &mock_vops_norename;
    tdvp.v_type = VDIR;
    
    error = vop_rename(&fdvp, &fvp, &fcnp, &tdvp, &tvp, &tcnp);

    if (error != EOPNOTSUPP) return false;

    return true;
}

bool test_vop_rename_bad_mount(void) {
    struct vnode fdvp, fvp, tdvp, tvp;
    struct componentname fcnp, tcnp;
    struct mount mp1, mp2;
    int error;

    memset(&fdvp, 0, sizeof(fdvp));
    memset(&fvp, 0, sizeof(fvp));
    memset(&tdvp, 0, sizeof(tdvp));
    memset(&tvp, 0, sizeof(tvp));
    memset(&mp1, 0, sizeof(mp1));
    memset(&mp2, 0, sizeof(mp2));

    fdvp.v_type = VDIR;
    fdvp.v_mount = &mp1;
    tdvp.v_type = VDIR;
    tdvp.v_mount = &mp2; // Different mount point

    error = vop_rename(&fdvp, &fvp, &fcnp, &tdvp, &tvp, &tcnp);

    if (error != EXDEV) return false;

    return true;
}
