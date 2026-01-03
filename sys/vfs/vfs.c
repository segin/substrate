#include "vfs.h"
#include <string.h>
#include "../kern/console.h"
#include <stdio.h>

fs_node_t *fs_root = 0; 

static filesystem_t *filesystems = NULL;

// External filesystem init functions
extern void ext2_init(void);
extern void fat_init(void);
extern void exfat_init(void);
extern void minix_init(void);
extern void devfs_init(void);
extern void procfs_init(void);
extern void sysfs_init(void);
extern void fuse_init(void);
extern void fuse_fs_init(void);
extern void p9_init(void);
extern void pseudo_init(void);

void vfs_init(void) {
    kprint("VFS: Initializing...\n");
    
    // Register real filesystem drivers
    ext2_init();
    fat_init();
    exfat_init();
    minix_init();
    
    // Register pseudo-filesystems
    devfs_init();
    procfs_init();
    sysfs_init();
    pseudo_init();
    
    // Register network/special filesystems
    fuse_init();
    fuse_fs_init();
    p9_init();
    
    // Mount pseudo-filesystems
    vfs_mount(NULL, "/dev", "devfs", 0, NULL);
    vfs_mount(NULL, "/proc", "procfs", 0, NULL);
    vfs_mount(NULL, "/sys", "sysfs", 0, NULL);
    
    kprint("VFS: Ready.\n");
}

void vfs_register_filesystem(filesystem_t *fs) {
    fs->next = filesystems;
    filesystems = fs;
}

int vfs_mount(const char *device, const char *path, const char *type, uint32_t flags, void *data) {
    // Find filesystem type
    filesystem_t *fs = filesystems;
    while (fs) {
        if (strcmp(fs->name, type) == 0) break;
        fs = fs->next;
    }
    if (!fs) {
        char buf[128];
        sprintf(buf, "VFS: Unknown filesystem type: %s\n", type);
        kprint(buf);
        return -1;
    }

    // Lookup device node if device path is specified
    fs_node_t *dev_node = NULL;
    if (device && device[0] == '/') {
        // Parse path like /dev/storage/ram0
        // Start from devfs if path begins with /dev/
        if (strncmp(device, "/dev/", 5) == 0) {
            // Find devfs mount
            extern fs_node_t *devfs_root_node_ptr;
            fs_node_t *devfs_root = devfs_root_node_ptr;
            if (devfs_root) {
                const char *subpath = device + 5; // skip "/dev/"
                // Parse path components
                char component[64];
                fs_node_t *current = devfs_root;
                while (*subpath && current) {
                    // Get next component
                    int i = 0;
                    while (*subpath && *subpath != '/' && i < 63) {
                        component[i++] = *subpath++;
                    }
                    component[i] = '\0';
                    if (*subpath == '/') subpath++;
                    
                    if (i > 0 && current->finddir) {
                        current = current->finddir(current, component);
                    }
                }
                dev_node = current;
            }
        }
    }

    // Call mount implementation with device node as data
    fs_node_t *root = fs->mount(device, flags, dev_node ? dev_node : data);
    if (!root) return -1;

    // Add to mount list
    if (strcmp(path, "/") == 0) {
        fs_root = root;
    }

    return 0;
}

uint32_t read_fs(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    if (node->read != 0) {
        uint32_t result = node->read(node, offset, size, buffer);
        
        // Update access time
        extern int64_t get_time(void);
        node->atime = get_time();
        
        return result;
    } else
        return 0;
}

uint32_t write_fs(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    if (node->write != 0) {
        uint32_t result = node->write(node, offset, size, buffer);
        
        // Update modification and change times
        extern int64_t get_time(void);
        int64_t now = get_time();
        node->mtime = now;
        node->ctime = now;
        
        return result;
    } else
        return 0;
}

void open_fs(fs_node_t *node, uint8_t read, uint8_t write) {
    (void)read; (void)write;
    if (node->open != 0)
        node->open(node);
}

void close_fs(fs_node_t *node) {
    if (node->close != 0)
        node->close(node);
}

struct dirent *readdir_fs(fs_node_t *node, uint32_t index) {
    if ((node->flags & 0x7) == FS_DIRECTORY && node->readdir != 0)
        return node->readdir(node, index);
    else
        return 0;
}

fs_node_t *finddir_fs(fs_node_t *node, char *name) {
    if (!node) return 0; // Safety
    
    // If this is a mountpoint, cross into the mounted filesystem
    if ((node->flags & FS_MOUNTPOINT) && node->ptr) {
        node = node->ptr;
    }

    if ((node->flags & 0x7) == FS_DIRECTORY && node->finddir != 0) {
        fs_node_t *result = node->finddir(node, name);
        
        // Resolve symlinks automatically
        if (result && (result->flags & 0x7) == FS_SYMLINK && result->readlink) {
            static char link_target[256];
            int len = result->readlink(result, link_target, sizeof(link_target));
            if (len > 0) {
                link_target[len] = '\0';
                
                // Resolve the target path
                fs_node_t *target;
                if (link_target[0] == '/') {
                    // Absolute symlink - start from root
                    target = vfs_lookup(fs_root, link_target);
                } else {
                    // Relative symlink - start from parent directory
                    target = finddir_fs(node, link_target);
                }
                
                if (target) {
                    return target;
                }
                // If symlink resolution fails, return the symlink node itself
            }
        }
        return result;
    }
    return 0;
}

// Lookup a path from a root node
fs_node_t *vfs_lookup(fs_node_t *root, const char *path) {
    if (!path || !root) return NULL;
    if (path[0] == '/') path++; // Skip leading /
    if (path[0] == '\0') return root; // Root itself
    
    fs_node_t *current = root;
    static char component[256];
    const char *p = path;
    
    while (*p) {
        // Extract next path component
        int i = 0;
        while (*p && *p != '/' && i < 255) {
            component[i++] = *p++;
        }
        component[i] = '\0';
        
        if (i == 0) {
            // Empty component (double slash)
            if (*p == '/') p++;
            continue;
        }
        
        // Lookup this component
        current = finddir_fs(current, component);
        if (!current) return NULL;
        
        // Skip trailing slash
        if (*p == '/') p++;
    }
    
    return current;
}

int vfs_check_permissions(fs_node_t *node, uint32_t uid, uint32_t gid, int mode) {
    if (uid == 0) return 0; // Root always has access

    uint32_t mask = 0;
    if (uid == node->uid) {
        if (mode & 4) mask |= 0400;
        if (mode & 2) mask |= 0200;
        if (mode & 1) mask |= 0100;
    } else if (gid == node->gid) {
        if (mode & 4) mask |= 0040;
        if (mode & 2) mask |= 0020;
        if (mode & 1) mask |= 0010;
    } else {
        if (mode & 4) mask |= 0004;
        if (mode & 2) mask |= 0002;
        if (mode & 1) mask |= 0001;
    }

    return (node->mask & mask) == mask ? 0 : -1;
}


int readlink_fs(fs_node_t *node, char *buf, size_t size) {
    if (node && node->readlink) {
        return node->readlink(node, buf, size);
    }
    return -1;
}

int symlink_fs(fs_node_t *parent, const char *target, const char *name) {
    if (parent && parent->symlink) {
        return parent->symlink(parent, target, name);
    }
    return -1;
}

