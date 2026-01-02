#include "../vfs/vfs.h"
#include <string.h>
#include <stddef.h>

#define MAX_DEVICES 64
static fs_node_t *devices[MAX_DEVICES];
static int device_count = 0;

void devfs_register_device(fs_node_t *node) {
    if (device_count < MAX_DEVICES) {
        devices[device_count++] = node;
    }
}

static struct dirent dev_dirent;

static struct dirent *devfs_readdir(fs_node_t *node, uint32_t index) {
    (void)node;
    if (index < (uint32_t)device_count) {
        strcpy(dev_dirent.name, devices[index]->name);
        dev_dirent.ino = index + 1;
        return &dev_dirent;
    }
    return NULL;
}

static fs_node_t *devfs_finddir(fs_node_t *node, char *name) {
    (void)node;
    for (int i = 0; i < device_count; i++) {
        if (strcmp(devices[i]->name, name) == 0) {
            return devices[i];
        }
    }
    return NULL;
}

static fs_node_t devfs_root_node;

static fs_node_t *devfs_mount(const char *device, uint32_t flags, void *data) {
    (void)device; (void)flags; (void)data;
    return &devfs_root_node;
}

static filesystem_t devfs_fs = {
    .name = "devfs",
    .mount = &devfs_mount,
};

void devfs_init(void) {
    memset(&devfs_root_node, 0, sizeof(fs_node_t));
    strcpy(devfs_root_node.name, "dev");
    devfs_root_node.flags = FS_DIRECTORY;
    devfs_root_node.readdir = &devfs_readdir;
    devfs_root_node.finddir = &devfs_finddir;

    vfs_register_filesystem(&devfs_fs);
}
