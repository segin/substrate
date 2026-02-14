#include <sys/types.h>
#include <vfs/vnode.h>
#include <sys/errno.h>
#include <sys/uio.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>

static int mock_readlink_called = 0;
static struct vnode *mock_vp_arg = NULL;
static struct uio *mock_uio_arg = NULL;

static int
mock_vop_readlink(struct vnode *vp, struct uio *uio, struct ucred *cred)
{
    mock_readlink_called++;
    mock_vp_arg = vp;
    mock_uio_arg = uio;
    (void)cred;
    return 0;
}

static struct vnodeops mock_vops_readlink = {
    .vop_readlink = mock_vop_readlink,
};

static struct vnodeops mock_vops_noreadlink = {
    .vop_readlink = NULL,
};

bool test_vop_readlink_basic(void) {
    struct vnode vp;
    struct uio uio;
    int error;

    memset(&vp, 0, sizeof(vp));
    vp.v_type = VLNK;
    vp.v_op = &mock_vops_readlink;
    
    memset(&uio, 0, sizeof(uio));

    mock_readlink_called = 0;
    mock_vp_arg = NULL;
    mock_uio_arg = NULL;

    error = vop_readlink(&vp, &uio, NULL);

    if (error != 0) return false;
    if (mock_readlink_called != 1) return false;
    if (mock_vp_arg != &vp) return false;
    if (mock_uio_arg != &uio) return false;

    return true;
}

bool test_vop_readlink_notlink(void) {
    struct vnode vp;
    struct uio uio;
    int error;

    memset(&vp, 0, sizeof(vp));
    vp.v_type = VREG; // Not a symbolic link

    error = vop_readlink(&vp, &uio, NULL);

    if (error != EINVAL) return false;

    return true;
}

bool test_vop_readlink_notsupp(void) {
    struct vnode vp;
    struct uio uio;
    int error;

    memset(&vp, 0, sizeof(vp));
    vp.v_type = VLNK;
    vp.v_op = &mock_vops_noreadlink;

    error = vop_readlink(&vp, &uio, NULL);

    if (error != EOPNOTSUPP) return false;

    return true;
}
