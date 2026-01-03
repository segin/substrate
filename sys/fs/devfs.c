#include "../vfs/vfs.h"
#include "../pm/pm.h"
#include <sys/proc.h>
#include <string.h>
#include <stddef.h>

extern fs_node_t *console_get_node(void);

static uint32_t tty_read_proxy(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    // Proxies to current process's TTY or default console
    fs_node_t *tty = current_process ? current_process->tty : NULL;
    if (!tty) tty = console_get_node();
    if (tty && tty->read) return tty->read(tty, offset, size, buffer);
    return 0;
}

static uint32_t tty_write_proxy(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    fs_node_t *tty = current_process ? current_process->tty : NULL;
    if (!tty) tty = console_get_node();
    if (tty && tty->write) return tty->write(tty, offset, size, buffer);
    return 0;
}

static int tty_ioctl_proxy(fs_node_t *node, uint32_t request, void *arg) {
    (void)node;
    fs_node_t *tty = current_process ? current_process->tty : NULL;
    if (!tty) tty = console_get_node();
    if (tty && tty->ioctl) return tty->ioctl(tty, request, arg);
    return -1;
}

static fs_node_t tty_node = {
    .name = "tty",
    .flags = FS_CHARDEVICE,
    .read = tty_read_proxy,
    .write = tty_write_proxy,
    .ioctl = tty_ioctl_proxy
};

// Device registry - split into char devices and storage devices
#define MAX_DEVICES 64
static fs_node_t *char_devices[MAX_DEVICES];
static int char_device_count = 0;

static fs_node_t *storage_devices[MAX_DEVICES];
static int storage_device_count = 0;

// Register a device - auto-categorize by type
void devfs_register_device(fs_node_t *node) {
    if (node->flags == FS_BLOCKDEVICE) {
        // Storage devices go under /dev/storage
        if (storage_device_count < MAX_DEVICES) {
            storage_devices[storage_device_count++] = node;
        }
    } else {
        // Character devices in /dev root
        if (char_device_count < MAX_DEVICES) {
            char_devices[char_device_count++] = node;
        }
    }
}

static struct dirent dev_dirent;

// /dev/storage directory operations
static struct dirent *storage_readdir(fs_node_t *node, uint32_t index) {
    (void)node;
    if (index < (uint32_t)storage_device_count) {
        strcpy(dev_dirent.name, storage_devices[index]->name);
        dev_dirent.ino = index + 1;
        return &dev_dirent;
    }
    return NULL;
}

static fs_node_t *storage_finddir(fs_node_t *node, char *name) {
    (void)node;
    for (int i = 0; i < storage_device_count; i++) {
        if (strcmp(storage_devices[i]->name, name) == 0) {
            return storage_devices[i];
        }
    }
    return NULL;
}

static fs_node_t storage_dir_node = {
    .name = "storage",
    .flags = FS_DIRECTORY,
    .readdir = storage_readdir,
    .finddir = storage_finddir
};

// /dev root directory operations
static struct dirent *devfs_readdir(fs_node_t *node, uint32_t index) {
    (void)node;
    // First entry: storage subdirectory
    if (index == 0) {
        strcpy(dev_dirent.name, "storage");
        dev_dirent.ino = 1;
        return &dev_dirent;
    }
    // Then tty
    if (index == 1) {
        strcpy(dev_dirent.name, "tty");
        dev_dirent.ino = 2;
        return &dev_dirent;
    }
    // Then char devices
    uint32_t char_idx = index - 2;
    if (char_idx < (uint32_t)char_device_count) {
        strcpy(dev_dirent.name, char_devices[char_idx]->name);
        dev_dirent.ino = char_idx + 3;
        return &dev_dirent;
    }
    return NULL;
}

static fs_node_t *devfs_finddir(fs_node_t *node, char *name) {
    (void)node;
    if (strcmp(name, "storage") == 0) return &storage_dir_node;
    if (strcmp(name, "tty") == 0) return &tty_node;
    for (int i = 0; i < char_device_count; i++) {
        if (strcmp(char_devices[i]->name, name) == 0) {
            return char_devices[i];
        }
    }
    return NULL;
}

static fs_node_t devfs_root_node;
fs_node_t *devfs_root_node_ptr = NULL; // For VFS device lookup

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
    
    devfs_root_node_ptr = &devfs_root_node; // Export for VFS device lookup

    vfs_register_filesystem(&devfs_fs);
}
