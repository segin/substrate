#include <vfs/vfs.h>
#include <pm/pm.h>
#include <sys/proc.h>
#include <sys/tty.h>
#include <string.h>
#include <stddef.h>
#include <vm/vm_kmem.h>

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

typedef struct devfs_entry {
    char name[128];
    fs_node_t *node;
    struct devfs_entry *parent;
    struct devfs_entry *child;
    struct devfs_entry *next;
} devfs_entry_t;

static devfs_entry_t *root_entry = NULL;
static struct dirent dev_dirent;

static struct dirent *devfs_dir_readdir(fs_node_t *node, uint64_t index);
static fs_node_t *devfs_dir_finddir(fs_node_t *node, char *name);

static devfs_entry_t *devfs_find_child(devfs_entry_t *parent, const char *name) {
    devfs_entry_t *curr = parent->child;
    while (curr) {
        if (strcmp(curr->name, name) == 0) return curr;
        curr = curr->next;
    }
    return NULL;
}

static devfs_entry_t *devfs_create_entry(const char *name, fs_node_t *node, devfs_entry_t *parent) {
    devfs_entry_t *entry = kmalloc(sizeof(devfs_entry_t));
    if (!entry) return NULL;
    memset(entry, 0, sizeof(devfs_entry_t));
    strncpy(entry->name, name, 127);
    entry->node = node;
    entry->parent = parent;

    // Link to parent
    entry->next = parent->child;
    parent->child = entry;

    // If it's a directory node we created, set impl to point to entry
    // so we can find children later.
    if (node->flags == FS_DIRECTORY) {
        node->impl = (uintptr_t)entry;
    }

    return entry;
}

static fs_node_t *devfs_create_dir_node(const char *name) {
    fs_node_t *node = kmalloc(sizeof(fs_node_t));
    if (!node) return NULL;
    memset(node, 0, sizeof(fs_node_t));
    strncpy(node->name, name, 127);
    node->flags = FS_DIRECTORY;
    node->readdir = &devfs_dir_readdir;
    node->finddir = &devfs_dir_finddir;
    return node;
}

static struct dirent *devfs_dir_readdir(fs_node_t *node, uint64_t index) {
    devfs_entry_t *entry = (devfs_entry_t *)node->impl;
    if (!entry) return NULL;

    devfs_entry_t *child = entry->child;
    uint64_t i = 0;
    while (child && i < index) {
        child = child->next;
        i++;
    }

    if (child) {
        strncpy(dev_dirent.d_name, child->name, sizeof(dev_dirent.d_name) - 1);
        dev_dirent.d_name[sizeof(dev_dirent.d_name) - 1] = '\0';
        dev_dirent.d_ino = (uintptr_t)child; // Use pointer as inode
        return &dev_dirent;
    }
    return NULL;
}

static fs_node_t *devfs_dir_finddir(fs_node_t *node, char *name) {
    devfs_entry_t *entry = (devfs_entry_t *)node->impl;
    if (!entry) return NULL;

    devfs_entry_t *child = devfs_find_child(entry, name);
    if (child) return child->node;

    return NULL;
}

static void devfs_add_entry(const char *path, fs_node_t *node) {
    devfs_entry_t *current = root_entry;
    if (!current) return; // Should not happen if initialized

    char name_buf[128];
    const char *p = path;

    while (1) {
        const char *sep = strchr(p, '/');
        if (sep) {
            size_t len = sep - p;
            if (len >= sizeof(name_buf)) len = sizeof(name_buf) - 1;
            strncpy(name_buf, p, len);
            name_buf[len] = '\0';

            // Find or create directory
            devfs_entry_t *next = devfs_find_child(current, name_buf);
            if (!next) {
                // Create directory node
                fs_node_t *dir_node = devfs_create_dir_node(name_buf);
                if (dir_node) {
                    next = devfs_create_entry(name_buf, dir_node, current);
                }
            }
            if (!next) return; // Failed to create directory
            current = next;
            p = sep + 1;
        } else {
            // Last component
            strncpy(name_buf, p, sizeof(name_buf) - 1);
            name_buf[sizeof(name_buf) - 1] = '\0';
            devfs_create_entry(name_buf, node, current);
            break;
        }
    }
}

void devfs_register_device(fs_node_t *node) {
    if (node->flags == FS_BLOCKDEVICE) {
        // Storage devices go under storage/
        char path[128];
        strcpy(path, "storage/");
        strncat(path, node->name, sizeof(path) - strlen(path) - 1);
        devfs_add_entry(path, node);
    } else {
        // Character devices use their name (which may include slashes)
        devfs_add_entry(node->name, node);
    }
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
    // Initialize root node
    memset(&devfs_root_node, 0, sizeof(fs_node_t));
    strcpy(devfs_root_node.name, "dev");
    devfs_root_node.flags = FS_DIRECTORY;
    devfs_root_node.readdir = &devfs_dir_readdir;
    devfs_root_node.finddir = &devfs_dir_finddir;

    // Create root entry
    root_entry = kmalloc(sizeof(devfs_entry_t));
    if (root_entry) {
        memset(root_entry, 0, sizeof(devfs_entry_t));
        strcpy(root_entry->name, "dev");
        root_entry->node = &devfs_root_node;

        devfs_root_node.impl = (uintptr_t)root_entry;
    }
    
    devfs_root_node_ptr = &devfs_root_node; // Export for VFS device lookup

    vfs_register_filesystem(&devfs_fs);

    // Register TTY
    devfs_register_device(&tty_node);
}
