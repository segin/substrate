#include <stddef.h>
#include <string.h>

#include <kern/time.h>
#include <sys/errno.h>
#include <sys/kobject.h>
#include <sys/mount.h>
#include <vfs/vfs.h>

static struct dirent sys_dirent;
static fs_node_t sysfs_root_node;

static int sysfs_statfs(fs_node_t *node, struct statfs *buf)
{
    (void)node;
    if (!buf) return -EINVAL;   /* was a bare -1, i.e. EPERM to userspace */
    memset(buf, 0, sizeof(*buf));
    buf->f_bsize  = 4096;
    buf->f_iosize = 4096;
    strncpy(buf->f_fstypename, "sysfs", sizeof(buf->f_fstypename));
    return 0;
}

static void sysfs_refresh_timestamps(fs_node_t *node) {
    time_t now;

    if (!node) {
        return;
    }

    now = get_time();
    node->atime = now;
    node->mtime = now;
    node->ctime = get_boot_time();
}

/* strncpy doesn't null-terminate when src length == dst size; the
 * d_name buffers are zero-initialized today so it works, but make it
 * explicit so a future memset removal doesn't silently break things. */
#define SYSFS_NAME_SET(dst, src) do {                           \
    strlcpy((dst), (src), sizeof(dst));                     \
    (dst)[sizeof(dst) - 1] = '\0';                              \
} while (0)

/* Stable inode numbers.  The nodes are file-scope statics, so a small fixed
 * table is enough -- and d_ino was previously never assigned at all, leaving
 * every /sys entry reporting inode 0. */
enum {
    SYSFS_INO_ROOT = 1,
    SYSFS_INO_BUS,
    SYSFS_INO_CLASS,
    SYSFS_INO_DEVICES,
};

/*
 * SYSFS-14: sysfs_readdir and sysfs_finddir were installed on the root AND on
 * bus/class/devices, and both IGNORED their node argument -- so /sys/bus
 * listed bus, class and devices, and so did /sys/bus/bus, without limit.
 * `find /sys`, `du -s /sys` or any recursive indexer never terminated.  The
 * root keeps the enumerating handlers; the leaf directories get their own
 * that offer only "." and "..", which is the truth: they have no contents
 * yet.
 */
static struct dirent *sysfs_readdir(fs_node_t *node, uint64_t index) {
    (void)node;
    if (index == 0) { SYSFS_NAME_SET(sys_dirent.d_name, "."); sys_dirent.d_ino = SYSFS_INO_ROOT; return &sys_dirent; }
    if (index == 1) { SYSFS_NAME_SET(sys_dirent.d_name, ".."); sys_dirent.d_ino = SYSFS_INO_ROOT; return &sys_dirent; }

    // Static list for prototype
    if (index == 2) { SYSFS_NAME_SET(sys_dirent.d_name, "bus"); sys_dirent.d_ino = SYSFS_INO_BUS; return &sys_dirent; }
    if (index == 3) { SYSFS_NAME_SET(sys_dirent.d_name, "class"); sys_dirent.d_ino = SYSFS_INO_CLASS; return &sys_dirent; }
    if (index == 4) { SYSFS_NAME_SET(sys_dirent.d_name, "devices"); sys_dirent.d_ino = SYSFS_INO_DEVICES; return &sys_dirent; }

    return NULL;
}

static fs_node_t sysfs_bus_node;
static fs_node_t sysfs_class_node;
static fs_node_t sysfs_devices_node;
static int sysfs_subnodes_inited = 0;

static fs_node_t *sysfs_finddir(fs_node_t *node, char *name);

/* Leaf directories: no children, so enumeration stops at the dot entries and
 * lookup finds nothing.  This is what bounds the tree. */
static struct dirent *sysfs_leaf_readdir(fs_node_t *node, uint64_t index) {
    if (index > 1) return NULL;
    SYSFS_NAME_SET(sys_dirent.d_name, index == 0 ? "." : "..");
    sys_dirent.d_ino = (index == 0 && node) ? node->inode : SYSFS_INO_ROOT;
    return &sys_dirent;
}

static fs_node_t *sysfs_leaf_finddir(fs_node_t *node, char *name) {
    (void)node; (void)name;
    return NULL;
}

static void sysfs_init_subnode(fs_node_t *n, const char *name, uint64_t ino) {
    memset(n, 0, sizeof(fs_node_t));
    strlcpy(n->name, name, sizeof(n->name));
    n->flags = FS_DIRECTORY;
    n->mask = 0555;
    n->uid = 0;
    n->gid = 0;
    n->inode = ino;
    n->readdir = &sysfs_leaf_readdir;
    n->finddir = &sysfs_leaf_finddir;
}

static fs_node_t *sysfs_finddir(fs_node_t *node, char *name) {
    (void)node;
    if (!sysfs_subnodes_inited) {
        sysfs_init_subnode(&sysfs_bus_node, "bus", SYSFS_INO_BUS);
        sysfs_init_subnode(&sysfs_class_node, "class", SYSFS_INO_CLASS);
        sysfs_init_subnode(&sysfs_devices_node, "devices", SYSFS_INO_DEVICES);
        sysfs_subnodes_inited = 1;
    }

    if (strcmp(name, "bus") == 0) {
        sysfs_refresh_timestamps(&sysfs_bus_node);
        return &sysfs_bus_node;
    }
    if (strcmp(name, "class") == 0) {
        sysfs_refresh_timestamps(&sysfs_class_node);
        return &sysfs_class_node;
    }
    if (strcmp(name, "devices") == 0) {
        sysfs_refresh_timestamps(&sysfs_devices_node);
        return &sysfs_devices_node;
    }
    return NULL;
}

static fs_node_t *sysfs_mount(const char *device, uint32_t flags, void *data) {
    (void)device; (void)flags; (void)data;
    sysfs_refresh_timestamps(&sysfs_root_node);
    return &sysfs_root_node;
}

static filesystem_t sysfs_fs = {
    .name = "sysfs",
    .mount = &sysfs_mount,
};

void sysfs_init(void) {
    memset(&sysfs_root_node, 0, sizeof(fs_node_t));
    SYSFS_NAME_SET(sysfs_root_node.name, "sys");
    sysfs_root_node.inode = SYSFS_INO_ROOT;
    sysfs_root_node.flags = FS_DIRECTORY;
    sysfs_root_node.mask = 0555;
    sysfs_root_node.uid = 0;
    sysfs_root_node.gid = 0;
    sysfs_root_node.readdir = &sysfs_readdir;
    sysfs_root_node.finddir = &sysfs_finddir;
    sysfs_root_node.statfs  = &sysfs_statfs;
    sysfs_refresh_timestamps(&sysfs_root_node);

    vfs_register_filesystem(&sysfs_fs);
}
