#include <vfs/vfs.h>
#include <pm/pm.h>
#include <sys/proc.h>
#include <sys/tty.h>
#include <string.h>
#include <stddef.h>
#include <vm/vm_kmem.h>
#include <kern/console.h>

extern fs_node_t *console_get_node(void);

static size_t tty_read_proxy(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    if (current_process && current_process->tty) {
        return tty_read(current_process->tty, (char *)buffer, size);
    }
    fs_node_t *cons = console_get_node();
    if (cons && cons->read) return cons->read(cons, offset, size, buffer);
    return 0;
}

static size_t tty_write_proxy(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node;
    if (current_process && current_process->tty) {
        return tty_write(current_process->tty, (const char *)buffer, size);
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
    .mask = 0666,
    .uid = 0,
    .gid = 0,
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
    uint8_t owns_node;
    char link_target[128];
} devfs_entry_t;

static devfs_entry_t *root_entry = NULL;
static struct dirent dev_dirent;
static fs_node_t devfs_root_node;
fs_node_t *devfs_root_node_ptr = NULL;

static struct dirent *devfs_dir_readdir(fs_node_t *node, uint64_t index);
static fs_node_t *devfs_dir_finddir(fs_node_t *node, char *name);

static devfs_entry_t *devfs_find_child(devfs_entry_t *parent, const char *name) {
    devfs_entry_t *curr;

    if (parent == NULL || name == NULL) {
        return NULL;
    }

    curr = parent->child;
    while (curr) {
        if (strcmp(curr->name, name) == 0) return curr;
        curr = curr->next;
    }
    return NULL;
}

static devfs_entry_t *devfs_create_entry(const char *name, fs_node_t *node, devfs_entry_t *parent) {
    devfs_entry_t *entry;

    if (name == NULL || node == NULL || parent == NULL) {
        return NULL;
    }

    entry = kmalloc(sizeof(devfs_entry_t));
    if (!entry) return NULL;
    memset(entry, 0, sizeof(devfs_entry_t));
    strncpy(entry->name, name, sizeof(entry->name) - 1);
    entry->node = node;
    entry->parent = parent;

    entry->next = parent->child;
    parent->child = entry;

    node->impl = (uintptr_t)entry;
    return entry;
}

static fs_node_t *devfs_create_dir_node(const char *name) {
    fs_node_t *node = kmalloc(sizeof(fs_node_t));
    if (!node) return NULL;
    memset(node, 0, sizeof(fs_node_t));
    strncpy(node->name, name, sizeof(node->name) - 1);
    node->flags = FS_DIRECTORY;
    node->mask = 0755;
    node->uid = 0;
    node->gid = 0;
    node->readdir = &devfs_dir_readdir;
    node->finddir = &devfs_dir_finddir;
    return node;
}

static int devfs_symlink_readlink(fs_node_t *node, char *buf, size_t size) {
    devfs_entry_t *entry;
    size_t len;

    if (node == NULL || buf == NULL || size == 0) {
        return -1;
    }

    entry = (devfs_entry_t *)node->impl;
    if (entry == NULL) {
        return -1;
    }

    len = strlen(entry->link_target);
    if (len > size) {
        len = size;
    }
    memcpy(buf, entry->link_target, len);
    return (int)len;
}

static struct dirent *devfs_dir_readdir(fs_node_t *node, uint64_t index) {
    devfs_entry_t *entry = (devfs_entry_t *)node->impl;
    devfs_entry_t *child;
    uint64_t i = 0;

    if (!entry) return NULL;

    child = entry->child;
    while (child && i < index) {
        child = child->next;
        i++;
    }

    if (child) {
        strncpy(dev_dirent.d_name, child->name, sizeof(dev_dirent.d_name) - 1);
        dev_dirent.d_name[sizeof(dev_dirent.d_name) - 1] = '\0';
        dev_dirent.d_ino = (uintptr_t)child;
        return &dev_dirent;
    }
    return NULL;
}

static fs_node_t *devfs_dir_finddir(fs_node_t *node, char *name) {
    devfs_entry_t *entry = (devfs_entry_t *)node->impl;
    devfs_entry_t *child;

    if (!entry) return NULL;

    child = devfs_find_child(entry, name);
    if (child) return child->node;

    return NULL;
}

static int devfs_valid_component(const char *name, size_t len) {
    if (!name || len == 0) return 0;
    if (len == 1 && name[0] == '.') return 0;
    if (len == 2 && name[0] == '.' && name[1] == '.') return 0;
    return 1;
}

static int devfs_path_allowed(const char *path) {
    const char *sep;
    size_t top_len;
    char top[128];
    devfs_entry_t *top_entry;

    if (!path || !path[0]) return 0;
    if (path[0] == '/') return 0;

    sep = strchr(path, '/');
    if (!sep) {
        return devfs_valid_component(path, strlen(path));
    }

    top_len = (size_t)(sep - path);
    if (!devfs_valid_component(path, top_len)) return 0;
    if (top_len >= sizeof(top)) return 0;

    strncpy(top, path, top_len);
    top[top_len] = '\0';

    if (!root_entry) return 0;
    top_entry = devfs_find_child(root_entry, top);
    if (!top_entry || !top_entry->node) return 0;
    if ((top_entry->node->flags & 0x7) != FS_DIRECTORY) return 0;

    while (*sep == '/') {
        const char *comp = sep + 1;
        const char *next = strchr(comp, '/');
        size_t len = next ? (size_t)(next - comp) : strlen(comp);
        if (!devfs_valid_component(comp, len)) return 0;
        if (!next) break;
        sep = next;
    }

    return 1;
}

static devfs_entry_t *devfs_lookup_path(const char *path) {
    devfs_entry_t *current = root_entry;
    const char *p = path;
    char name_buf[128];

    if (current == NULL || path == NULL || path[0] == '\0') {
        return NULL;
    }

    while (1) {
        const char *sep = strchr(p, '/');
        size_t len = sep ? (size_t)(sep - p) : strlen(p);

        if (len >= sizeof(name_buf)) {
            return NULL;
        }
        strncpy(name_buf, p, len);
        name_buf[len] = '\0';

        current = devfs_find_child(current, name_buf);
        if (current == NULL) {
            return NULL;
        }
        if (!sep) {
            return current;
        }
        p = sep + 1;
    }
}

static devfs_entry_t *devfs_find_entry_by_node(devfs_entry_t *entry, fs_node_t *node) {
    devfs_entry_t *found;

    while (entry != NULL) {
        if (entry->node == node) {
            return entry;
        }
        found = devfs_find_entry_by_node(entry->child, node);
        if (found != NULL) {
            return found;
        }
        entry = entry->next;
    }

    return NULL;
}

static void devfs_destroy_entry(devfs_entry_t *entry) {
    devfs_entry_t *child;
    devfs_entry_t *next;

    if (entry == NULL) {
        return;
    }

    child = entry->child;
    while (child != NULL) {
        next = child->next;
        devfs_destroy_entry(child);
        child = next;
    }

    if (entry->owns_node && entry->node != NULL) {
        kfree(entry->node, sizeof(fs_node_t));
    }
    kfree(entry, sizeof(*entry));
}

static void devfs_remove_entry(devfs_entry_t *entry) {
    devfs_entry_t *curr;
    devfs_entry_t *prev = NULL;

    if (entry == NULL || entry->parent == NULL) {
        return;
    }

    curr = entry->parent->child;
    while (curr != NULL) {
        if (curr == entry) {
            if (prev != NULL) {
                prev->next = curr->next;
            } else {
                entry->parent->child = curr->next;
            }
            entry->next = NULL;
            devfs_destroy_entry(entry);
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

static int devfs_add_entry(const char *path, fs_node_t *node) {
    devfs_entry_t *current = root_entry;
    char name_buf[128];
    const char *p = path;

    if (current == NULL || path == NULL || node == NULL) {
        return -1;
    }

    while (1) {
        const char *sep = strchr(p, '/');
        size_t len = sep ? (size_t)(sep - p) : strlen(p);
        devfs_entry_t *next;

        if (len >= sizeof(name_buf)) {
            return -1;
        }
        strncpy(name_buf, p, len);
        name_buf[len] = '\0';

        next = devfs_find_child(current, name_buf);
        if (sep) {
            if (!next) {
                fs_node_t *dir_node = devfs_create_dir_node(name_buf);
                if (dir_node == NULL) {
                    return -1;
                }
                next = devfs_create_entry(name_buf, dir_node, current);
            }
            if (next == NULL || (next->node->flags & 0x7) != FS_DIRECTORY) {
                return -1;
            }
            current = next;
            p = sep + 1;
            continue;
        }

        if (next != NULL) {
            next->node = node;
            node->impl = (uintptr_t)next;
            return 0;
        }

        return devfs_create_entry(name_buf, node, current) != NULL ? 0 : -1;
    }
}

static void devfs_create_common_dir(const char *name) {
    if (!devfs_find_child(root_entry, name)) {
        fs_node_t *dir_node = devfs_create_dir_node(name);
        if (dir_node) {
            (void)devfs_create_entry(name, dir_node, root_entry);
        }
    }
}

void devfs_register_device(fs_node_t *node) {
    char path[128];

    if (!node || !node->name[0]) {
        return;
    }

    if (!devfs_path_allowed(node->name)) {
        kprintf("devfs: rejected device path '%s'\n", node->name);
        return;
    }

    if (strchr(node->name, '/')) {
        (void)devfs_add_entry(node->name, node);
        return;
    }

    if ((node->flags & 0x7) == FS_BLOCKDEVICE) {
        strncpy(path, "storage/", sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        strncat(path, node->name, sizeof(path) - strlen(path) - 1);
        (void)devfs_add_entry(path, node);
        return;
    }

    (void)devfs_add_entry(node->name, node);
}

void devfs_unregister_device(fs_node_t *node) {
    devfs_entry_t *entry;

    if (node == NULL || root_entry == NULL) {
        return;
    }

    entry = devfs_find_entry_by_node(root_entry->child, node);
    if (entry != NULL) {
        devfs_remove_entry(entry);
    }
}

int devfs_register_alias(const char *path, const char *target) {
    fs_node_t *node;
    devfs_entry_t *entry;

    if (!devfs_path_allowed(path) || target == NULL || target[0] == '\0') {
        return -1;
    }

    entry = devfs_lookup_path(path);
    if (entry != NULL) {
        if (entry->node == NULL || (entry->node->flags & 0x7) != FS_SYMLINK) {
            return -1;
        }
        strncpy(entry->link_target, target, sizeof(entry->link_target) - 1);
        entry->link_target[sizeof(entry->link_target) - 1] = '\0';
        return 0;
    }

    node = kmalloc(sizeof(fs_node_t));
    if (node == NULL) {
        return -1;
    }
    memset(node, 0, sizeof(*node));
    strncpy(node->name, path, sizeof(node->name) - 1);
    node->flags = FS_SYMLINK;
    node->mask = 0777;
    node->uid = 0;
    node->gid = 0;
    node->readlink = devfs_symlink_readlink;

    if (devfs_add_entry(path, node) != 0) {
        kfree(node, sizeof(*node));
        return -1;
    }

    entry = (devfs_entry_t *)node->impl;
    if (entry == NULL) {
        kfree(node, sizeof(*node));
        return -1;
    }
    entry->owns_node = 1;
    strncpy(entry->link_target, target, sizeof(entry->link_target) - 1);
    entry->link_target[sizeof(entry->link_target) - 1] = '\0';
    return 0;
}

void devfs_unregister_alias(const char *path) {
    devfs_entry_t *entry;

    entry = devfs_lookup_path(path);
    if (entry == NULL || entry->node == NULL || (entry->node->flags & 0x7) != FS_SYMLINK) {
        return;
    }

    devfs_remove_entry(entry);
}

static fs_node_t *devfs_mount(const char *device, uint32_t flags, void *data) {
    (void)device;
    (void)flags;
    (void)data;
    return &devfs_root_node;
}

static filesystem_t devfs_fs = {
    .name = "devfs",
    .mount = &devfs_mount,
};

void devfs_init(void) {
    memset(&devfs_root_node, 0, sizeof(fs_node_t));
    strncpy(devfs_root_node.name, "dev", sizeof(devfs_root_node.name) - 1);
    devfs_root_node.name[sizeof(devfs_root_node.name) - 1] = '\0';
    devfs_root_node.flags = FS_DIRECTORY;
    devfs_root_node.mask = 0755;
    devfs_root_node.uid = 0;
    devfs_root_node.gid = 0;
    devfs_root_node.readdir = &devfs_dir_readdir;
    devfs_root_node.finddir = &devfs_dir_finddir;

    root_entry = kmalloc(sizeof(devfs_entry_t));
    if (root_entry) {
        memset(root_entry, 0, sizeof(devfs_entry_t));
        strncpy(root_entry->name, "dev", sizeof(root_entry->name) - 1);
        root_entry->name[sizeof(root_entry->name) - 1] = '\0';
        root_entry->node = &devfs_root_node;
        devfs_root_node.impl = (uintptr_t)root_entry;

        devfs_create_common_dir("comm");
        devfs_create_common_dir("storage");
        devfs_create_common_dir("input");
        devfs_create_common_dir("by-id");
    }

    devfs_root_node_ptr = &devfs_root_node;
    vfs_register_filesystem(&devfs_fs);
    devfs_register_device(&tty_node);
}
