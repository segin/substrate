#include <vfs/vfs.h>
#include <pm/pm.h>
#include <sys/proc.h>
#include <sys/tty.h>
#include <string.h>
#include <stddef.h>

extern fs_node_t *console_get_node(void);

static size_t tty_read_proxy(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    if (current_process && current_process->tty) {
        return tty_read(current_process->tty, (char*)buffer, size);
    }
    fs_node_t *cons = console_get_node();
    if (cons && cons->read) return cons->read(cons, offset, size, buffer);
    return 0;
}

static size_t tty_write_proxy(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node;
    if (current_process && current_process->tty) {
        return tty_write(current_process->tty, (const char*)buffer, size);
    }
    fs_node_t *cons = console_get_node();
    if (cons && cons->write) return cons->write(cons, offset, size, buffer);
    return 0;
}

static int tty_ioctl_proxy(fs_node_t *node, uint32_t request, void *arg) {
    (void)node;
    if (current_process && current_process->tty) {
        return tty_ioctl(current_process->tty, request, (unsigned long)arg);
    }
    fs_node_t *cons = console_get_node();
    if (cons && cons->ioctl) return cons->ioctl(cons, request, arg);
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
static struct dirent *storage_readdir(fs_node_t *node, uint64_t index) {
    (void)node;
    if (index < (uint64_t)storage_device_count) {
        strncpy(dev_dirent.d_name, storage_devices[index]->name, sizeof(dev_dirent.d_name) - 1);
        dev_dirent.d_name[sizeof(dev_dirent.d_name) - 1] = '\0';
        dev_dirent.d_ino = index + 1;
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
static struct dirent *devfs_readdir(fs_node_t *node, uint64_t index) {
    (void)node;
    // First entry: storage subdirectory
    if (index == 0) {
        strcpy(dev_dirent.d_name, "storage");
        dev_dirent.d_ino = 1;
        return &dev_dirent;
    }
    // Then tty
    if (index == 1) {
        strcpy(dev_dirent.d_name, "tty");
        dev_dirent.d_ino = 2;
        return &dev_dirent;
    }
    // Then char devices
    uint64_t char_idx = index - 2;
    if (char_idx < (uint64_t)char_device_count) {
        strncpy(dev_dirent.d_name, char_devices[char_idx]->name, sizeof(dev_dirent.d_name) - 1);
        dev_dirent.d_name[sizeof(dev_dirent.d_name) - 1] = '\0';
        dev_dirent.d_ino = char_idx + 3;
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
