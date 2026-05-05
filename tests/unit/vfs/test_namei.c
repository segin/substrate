#include <sys/types.h>
#include <sys/errno.h>
#include <sys/namei.h>
#include <sys/mount.h>
#include <sys/uio.h>
#include <vfs/vnode.h>
#include <stdbool.h>
#include <string.h>

extern struct vnode *rootvnode;

struct mock_dirent {
    const char *name;
    struct vnode *target;
};

struct mock_node {
    struct vnode *parent;
    const char *symlink_target;
    struct mock_dirent entries[8];
    size_t entry_count;
};

static int
mock_lookup(struct vnode *dvp, struct vnode **vpp, struct componentname *cnp)
{
    struct mock_node *node = (struct mock_node *)dvp->v_data;
    size_t index;

    if (cnp->cn_namelen == 1 && cnp->cn_nameptr[0] == '.') {
        *vpp = dvp;
        vref(*vpp);
        return 0;
    }

    if (cnp->cn_namelen == 2 && cnp->cn_nameptr[0] == '.' &&
        cnp->cn_nameptr[1] == '.') {
        *vpp = node && node->parent ? node->parent : dvp;
        vref(*vpp);
        return 0;
    }

    if (!node)
        return ENOENT;

    for (index = 0; index < node->entry_count; index++) {
        if (strlen(node->entries[index].name) == cnp->cn_namelen &&
            memcmp(node->entries[index].name, cnp->cn_nameptr, cnp->cn_namelen) == 0) {
            *vpp = node->entries[index].target;
            vref(*vpp);
            return 0;
        }
    }

    return ENOENT;
}

static int
mock_readlink(struct vnode *vp, struct uio *uio, struct ucred *cred)
{
    struct mock_node *node = (struct mock_node *)vp->v_data;
    size_t target_len;
    size_t copy_len;

    (void)cred;

    if (!node || !node->symlink_target || !uio || !uio->uio_iov || uio->uio_iovcnt < 1)
        return EINVAL;

    target_len = strlen(node->symlink_target);
    copy_len = target_len;
    if (copy_len > uio->uio_iov[0].iov_len)
        copy_len = uio->uio_iov[0].iov_len;
    if (copy_len > uio->uio_resid)
        copy_len = uio->uio_resid;

    memcpy(uio->uio_iov[0].iov_base, node->symlink_target, copy_len);
    uio->uio_iov[0].iov_base = (char *)uio->uio_iov[0].iov_base + copy_len;
    uio->uio_iov[0].iov_len -= copy_len;
    uio->uio_resid -= copy_len;
    uio->uio_offset += (off_t)copy_len;
    return 0;
}

static struct vnodeops mock_namei_vops = {
    .vop_lookup = mock_lookup,
    .vop_readlink = mock_readlink,
};

static int
mock_vfs_root(struct mount *mp, struct vnode **vpp)
{
    if (!mp || !vpp || !mp->mnt_root)
        return EINVAL;
    *vpp = mp->mnt_root;
    vref(*vpp);
    return 0;
}

static struct vfsops mock_namei_mount_ops = {
    .vfs_root = mock_vfs_root,
};

static void
init_mock_vnode(struct vnode *vp, struct mock_node *node, enum vtype type)
{
    memset(node, 0, sizeof(*node));
    vp->v_type = type;
    vp->v_op = &mock_namei_vops;
    vp->v_data = node;
}

static bool
alloc_mock_vnode(struct vnode **vpp, const char *tag, struct mock_node *node, enum vtype type)
{
    if (getnewvnode(tag, NULL, &mock_namei_vops, vpp) != 0 || !*vpp)
        return false;
    init_mock_vnode(*vpp, node, type);
    return true;
}

static void
free_mock_vnode(struct vnode *vp)
{
    if (!vp)
        return;
    vp->v_usecount = 0;
    vp->v_flag &= ~VONFREELIST;
    vnode_reclaim(vp);
}

static void
add_child(struct vnode *parent, const char *name, struct vnode *child)
{
    struct mock_node *node = (struct mock_node *)parent->v_data;
    node->entries[node->entry_count].name = name;
    node->entries[node->entry_count].target = child;
    node->entry_count++;
}

bool test_namei_simple_path(void)
{
    struct vnode *saved_root = rootvnode;
    struct vnode *root = NULL, *dir = NULL, *file = NULL;
    struct mock_node root_node, dir_node, file_node;
    struct nameidata nd;
    int error;

    vnode_init();
    nchinit();
    namei_init();

    if (!alloc_mock_vnode(&root, "namei_root", &root_node, VDIR) ||
        !alloc_mock_vnode(&dir, "namei_dir", &dir_node, VDIR) ||
        !alloc_mock_vnode(&file, "namei_file", &file_node, VREG)) {
        free_mock_vnode(root);
        free_mock_vnode(dir);
        free_mock_vnode(file);
        return false;
    }

    root_node.parent = root;
    dir_node.parent = root;
    file_node.parent = dir;
    add_child(root, "dir", dir);
    add_child(dir, "file", file);
    rootvnode = root;

    NDINIT(&nd, LOOKUP, FOLLOW, UIO_SYSSPACE, "/dir/file");
    nd.ni_rootdir = root;
    nd.ni_startdir = root;
    error = namei(&nd);

    if (error == 0 && nd.ni_vp)
        vrele(nd.ni_vp);
    rootvnode = saved_root;
    free_mock_vnode(root);
    free_mock_vnode(dir);
    free_mock_vnode(file);

    return error == 0 && nd.ni_vp == file;
}

bool test_namei_dot_and_dotdot(void)
{
    struct vnode *saved_root = rootvnode;
    struct vnode *root = NULL, *dir = NULL, *sub = NULL, *file = NULL;
    struct mock_node root_node, dir_node, sub_node, file_node;
    struct nameidata nd;
    int error;

    vnode_init();
    nchinit();
    namei_init();

    if (!alloc_mock_vnode(&root, "namei_root", &root_node, VDIR) ||
        !alloc_mock_vnode(&dir, "namei_dir", &dir_node, VDIR) ||
        !alloc_mock_vnode(&sub, "namei_sub", &sub_node, VDIR) ||
        !alloc_mock_vnode(&file, "namei_file", &file_node, VREG)) {
        free_mock_vnode(root);
        free_mock_vnode(dir);
        free_mock_vnode(sub);
        free_mock_vnode(file);
        return false;
    }

    root_node.parent = root;
    dir_node.parent = root;
    sub_node.parent = dir;
    file_node.parent = dir;
    add_child(root, "dir", dir);
    add_child(dir, "sub", sub);
    add_child(dir, "file", file);
    rootvnode = root;

    NDINIT(&nd, LOOKUP, FOLLOW, UIO_SYSSPACE, "/dir/./sub/../file");
    nd.ni_rootdir = root;
    nd.ni_startdir = root;
    error = namei(&nd);

    if (error == 0 && nd.ni_vp)
        vrele(nd.ni_vp);
    rootvnode = saved_root;
    free_mock_vnode(root);
    free_mock_vnode(dir);
    free_mock_vnode(sub);
    free_mock_vnode(file);

    return error == 0 && nd.ni_vp == file;
}

bool test_namei_mount_crossing(void)
{
    struct vnode *saved_root = rootvnode;
    struct vnode *root = NULL, *covered = NULL, *mounted_root = NULL, *file = NULL;
    struct mock_node root_node, covered_node, mounted_root_node, file_node;
    struct mount mp;
    struct nameidata nd;
    int error;

    vnode_init();
    nchinit();
    namei_init();

    if (!alloc_mock_vnode(&root, "namei_root", &root_node, VDIR) ||
        !alloc_mock_vnode(&covered, "namei_covered", &covered_node, VDIR) ||
        !alloc_mock_vnode(&mounted_root, "namei_mroot", &mounted_root_node, VDIR) ||
        !alloc_mock_vnode(&file, "namei_file", &file_node, VREG)) {
        free_mock_vnode(root);
        free_mock_vnode(covered);
        free_mock_vnode(mounted_root);
        free_mock_vnode(file);
        return false;
    }

    memset(&mp, 0, sizeof(mp));
    mp.mnt_op = &mock_namei_mount_ops;
    mp.mnt_root = mounted_root;
    mp.mnt_vnodecovered = covered;

    root_node.parent = root;
    covered_node.parent = root;
    mounted_root_node.parent = mounted_root;
    file_node.parent = mounted_root;
    covered->v_mountedhere = &mp;
    mounted_root->v_mount = &mp;
    mounted_root->v_flag |= VROOT;
    add_child(root, "mnt", covered);
    add_child(mounted_root, "file", file);
    rootvnode = root;

    NDINIT(&nd, LOOKUP, FOLLOW, UIO_SYSSPACE, "/mnt/file");
    nd.ni_rootdir = root;
    nd.ni_startdir = root;
    error = namei(&nd);

    if (error == 0 && nd.ni_vp)
        vrele(nd.ni_vp);
    rootvnode = saved_root;
    free_mock_vnode(root);
    free_mock_vnode(covered);
    free_mock_vnode(mounted_root);
    free_mock_vnode(file);

    return error == 0 && nd.ni_vp == file;
}

bool test_namei_symlink_resolution(void)
{
    struct vnode *saved_root = rootvnode;
    struct vnode *root = NULL, *link = NULL, *target = NULL;
    struct mock_node root_node, link_node, target_node;
    struct nameidata nd;
    int error;

    vnode_init();
    nchinit();
    namei_init();

    if (!alloc_mock_vnode(&root, "namei_root", &root_node, VDIR) ||
        !alloc_mock_vnode(&link, "namei_link", &link_node, VLNK) ||
        !alloc_mock_vnode(&target, "namei_target", &target_node, VREG)) {
        free_mock_vnode(root);
        free_mock_vnode(link);
        free_mock_vnode(target);
        return false;
    }

    root_node.parent = root;
    link_node.parent = root;
    link_node.symlink_target = "/target";
    target_node.parent = root;
    add_child(root, "link", link);
    add_child(root, "target", target);
    rootvnode = root;

    NDINIT(&nd, LOOKUP, FOLLOW, UIO_SYSSPACE, "/link");
    nd.ni_rootdir = root;
    nd.ni_startdir = root;
    error = namei(&nd);

    if (error == 0 && nd.ni_vp)
        vrele(nd.ni_vp);
    rootvnode = saved_root;
    free_mock_vnode(root);
    free_mock_vnode(link);
    free_mock_vnode(target);

    return error == 0 && nd.ni_vp == target;
}

bool test_namei_maxsymlinks(void)
{
    struct vnode *saved_root = rootvnode;
    struct vnode *root = NULL, *loop = NULL;
    struct mock_node root_node, loop_node;
    struct nameidata nd;
    int error;

    vnode_init();
    nchinit();
    namei_init();

    if (!alloc_mock_vnode(&root, "namei_root", &root_node, VDIR) ||
        !alloc_mock_vnode(&loop, "namei_loop", &loop_node, VLNK)) {
        free_mock_vnode(root);
        free_mock_vnode(loop);
        return false;
    }

    root_node.parent = root;
    loop_node.parent = root;
    loop_node.symlink_target = "/loop";
    add_child(root, "loop", loop);
    rootvnode = root;

    NDINIT(&nd, LOOKUP, FOLLOW, UIO_SYSSPACE, "/loop");
    nd.ni_rootdir = root;
    nd.ni_startdir = root;
    error = namei(&nd);

    rootvnode = saved_root;
    free_mock_vnode(root);
    free_mock_vnode(loop);

    return error == ELOOP;
}