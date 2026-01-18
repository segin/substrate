#include "vfs.h"
#include <string.h>
#include "../kern/console.h"
#include <stdio.h>
#include <sys/poll.h>
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
extern void ntsync_init(void);

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
    ntsync_init();
    
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

uint32_t read_fs(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    if (node->read != 0) {
        uint32_t result = node->read(node, offset, size, buffer);
        
        // Update access time
        extern int64_t get_time(void);
        node->atime = get_time();
        
        return result;
    } else
        return 0;
}

uint32_t write_fs(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
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



// Internal with follow control
static fs_node_t *finddir_fs_internal(fs_node_t *node, char *name, int depth, int follow_symlinks);

fs_node_t *finddir_fs(fs_node_t *node, char *name) {
    return finddir_fs_internal(node, name, 0, 1);
}

// Maximum symlink recursion depth to prevent infinite loops
#define MAX_SYMLINK_DEPTH 8

static fs_node_t *finddir_fs_internal(fs_node_t *node, char *name, int depth, int follow_symlinks) {
    if (!node) return 0; // Safety
    
    // If this is a mountpoint, cross into the mounted filesystem
    if ((node->flags & FS_MOUNTPOINT) && node->ptr) {
        node = node->ptr;
    }

    if ((node->flags & 0x7) == FS_DIRECTORY && node->finddir != 0) {
        fs_node_t *result = node->finddir(node, name);
        
        // Resolve symlinks (with depth limit)
        if (result && (result->flags & 0x7) == FS_SYMLINK && result->readlink) {
            // Check if we should follow symlinks
            if (!follow_symlinks) return result;

            // Check recursion depth limit
            if (depth >= MAX_SYMLINK_DEPTH) {
                // Too many symlink levels - return the symlink node itself (ELOOP)
                return result;
            }
            
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
                    // Relative symlink - recurse
                    target = finddir_fs_internal(node, link_target, depth + 1, 1); // Follow nested
                }
                
                if (target) {
                    return target;
                }
                // If symlink resolution fails, return the symlink node itself (or NULL? Linux returns ENOENT)
                // Returning result roughly mimics getting the link itself so we can see it exists but is broken?
                // Correct behavior is usually ENOENT, but returning the node is safer for some "ls" ops.
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

fs_node_t *vfs_lookup_lstat(fs_node_t *root, const char *path) {
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
            if (*p == '/') p++;
            continue;
        }
        
        // Check if this is the last component
        int is_last = (*p == '\0');
        
        // Lookup this component
        // If last, DO NOT follow symlinks
        current = finddir_fs_internal(current, component, 0, !is_last);
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

int link_fs(fs_node_t *parent, fs_node_t *source, const char *name) {
    if (parent && parent->link) {
        return parent->link(parent, source, name);
    }
    return -1;
}

int unlink_fs(fs_node_t *node, const char *name) {
    if (node && node->unlink) {
        return node->unlink(node, name);
    }
    return -1;
}

void *mmap_fs(fs_node_t *node, void *addr, size_t length, int prot, int flags, off_t offset) {
    if (node && node->mmap) {
        return node->mmap(node, addr, length, prot, flags, offset);
    }
    return (void *)-1;
}

int poll_fs(fs_node_t *node, void *waiter) {
    if (!node) return POLLNVAL;
    
    if (node->poll) {
        return node->poll(node, waiter);
    }
    
    // Default behavior: Regular files and directories are always readable/writable
    if ((node->flags & 0x7) == FS_FILE || (node->flags & 0x7) == FS_DIRECTORY) {
        return POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM; 
    }
    
    return 0;
}


// Helper to find last occurrence of character
static char *vfs_strrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) {
        if (*s == c) last = s;
        s++;
    }
    if (*s == c) last = s; // check terminator... unlikely but standard logic
    return (char *)last;
}

int vfs_mkdir(const char *path, uint16_t permission) {
    if (!path) return -1;
    
    char path_buf[256];
    strncpy(path_buf, path, sizeof(path_buf));
    path_buf[255] = '\0';
    
    // Split path into parent and name
    char *last_slash = vfs_strrchr(path_buf, '/');
    char *name = NULL;
    fs_node_t *parent_node = NULL;
    
    if (last_slash) {
        *last_slash = '\0';
        name = last_slash + 1;
        
        // Check for trailing slash case: /foo/bar/ -> name=""
        if (*name == '\0') return -1; // Trailing slash not supported yet
        
        if (path_buf[0] == '\0') {
            // Path was "/foo", parent became "" (meaning root)
            parent_node = fs_root;
        } else {
            // Find parent
            // TODO: Use current_process->cwd if relative? 
            // For now assuming full path or we rely on caller to resolve.
            parent_node = vfs_lookup(fs_root, path_buf);
        }
    } else {
        // No slash: "foo" -> imply root if no CWD support yet
        parent_node = fs_root;
        name = path_buf;
    }
    
    if (!parent_node) return -1; // Parent not found
    
    if ((parent_node->flags & 0x7) != FS_DIRECTORY) return -1; // Not a directory
    
    if (!parent_node->mkdir) return -1; // FS doesn't support mkdir
    
    return parent_node->mkdir(parent_node, name, permission);
}

