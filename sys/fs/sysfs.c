#include <vfs/vfs.h>
#include <sys/kobject.h>
#include <string.h>
#include <stddef.h>

static struct dirent sys_dirent;

static struct dirent *sysfs_readdir(fs_node_t *node, uint64_t index) {
    (void)node;
    if (index == 0) { strlcpy(sys_dirent.d_name, ".", sizeof(sys_dirent.d_name)); return &sys_dirent; }
    if (index == 1) { strlcpy(sys_dirent.d_name, "..", sizeof(sys_dirent.d_name)); return &sys_dirent; }
    
    // Static list for prototype
    if (index == 2) { strlcpy(sys_dirent.d_name, "bus", sizeof(sys_dirent.d_name)); return &sys_dirent; }
    if (index == 3) { strlcpy(sys_dirent.d_name, "class", sizeof(sys_dirent.d_name)); return &sys_dirent; }
    if (index == 4) { strlcpy(sys_dirent.d_name, "devices", sizeof(sys_dirent.d_name)); return &sys_dirent; }
    
    return NULL;
}

static fs_node_t *sysfs_finddir(fs_node_t *node, char *name) {
    (void)node;
    if (strcmp(name, "bus") == 0 || strcmp(name, "class") == 0 || strcmp(name, "devices") == 0) {
        static fs_node_t sub_node;
        memset(&sub_node, 0, sizeof(fs_node_t));
        strlcpy(sub_node.name, name, sizeof(sub_node.name));
        sub_node.flags = FS_DIRECTORY;
        sub_node.readdir = &sysfs_readdir; // Simple reuse for proto
        return &sub_node;
    }
    return NULL;
}

static fs_node_t sysfs_root_node;

static fs_node_t *sysfs_mount(const char *device, uint32_t flags, void *data) {
    (void)device; (void)flags; (void)data;
    return &sysfs_root_node;
}

static filesystem_t sysfs_fs = {
    .name = "sysfs",
    .mount = &sysfs_mount,
};

void sysfs_init(void) {
    memset(&sysfs_root_node, 0, sizeof(fs_node_t));
    strlcpy(sysfs_root_node.name, "sys", sizeof(sysfs_root_node.name));
    sysfs_root_node.flags = FS_DIRECTORY;
    sysfs_root_node.readdir = &sysfs_readdir;
    sysfs_root_node.finddir = &sysfs_finddir;

    vfs_register_filesystem(&sysfs_fs);
}
