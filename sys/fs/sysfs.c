#include <vfs/vfs.h>
#include <sys/kobject.h>
#include <sys/mount.h>
#include <kern/time.h>
#include <string.h>
#include <stddef.h>

static struct dirent sys_dirent;
static fs_node_t sysfs_root_node;

static int sysfs_statfs(fs_node_t *node, struct statfs *buf)
{
    (void)node;
    if (!buf) return -1;
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
    strncpy((dst), (src), sizeof(dst) - 1);                     \
    (dst)[sizeof(dst) - 1] = '\0';                              \
} while (0)

static struct dirent *sysfs_readdir(fs_node_t *node, uint64_t index) {
    (void)node;
    if (index == 0) { SYSFS_NAME_SET(sys_dirent.d_name, "."); return &sys_dirent; }
    if (index == 1) { SYSFS_NAME_SET(sys_dirent.d_name, ".."); return &sys_dirent; }

    // Static list for prototype
    if (index == 2) { SYSFS_NAME_SET(sys_dirent.d_name, "bus"); return &sys_dirent; }
    if (index == 3) { SYSFS_NAME_SET(sys_dirent.d_name, "class"); return &sys_dirent; }
    if (index == 4) { SYSFS_NAME_SET(sys_dirent.d_name, "devices"); return &sys_dirent; }

    return NULL;
}

static fs_node_t *sysfs_finddir(fs_node_t *node, char *name) {
    (void)node;
    if (strcmp(name, "bus") == 0 || strcmp(name, "class") == 0 || strcmp(name, "devices") == 0) {
        static fs_node_t sub_node;
        memset(&sub_node, 0, sizeof(fs_node_t));
        strncpy(sub_node.name, name, sizeof(sub_node.name) - 1);
        sub_node.flags = FS_DIRECTORY;
        sub_node.mask = 0555;
        sub_node.uid = 0;
        sub_node.gid = 0;
        sub_node.readdir = &sysfs_readdir; // Simple reuse for proto
        sub_node.finddir = &sysfs_finddir;
        sysfs_refresh_timestamps(&sub_node);
        return &sub_node;
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
