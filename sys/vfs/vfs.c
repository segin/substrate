#include "vfs.h"
#include <string.h>
#include "../drivers/video/vga.h"

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
    vga_write("VFS: Initializing...\n", 21);
    
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
    
    vga_write("VFS: Ready.\n", 12);
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
    if (!fs) return -1;

    // Call mount implementation
    fs_node_t *root = fs->mount(device, flags, data);
    if (!root) return -1;

    // Add to mount list
    // (Note: In a real system, we'd find the node at 'path' and mark it as a mountpoint)
    // For now, we support mounting at / (root) or just adding to a list for lookup.
    
    if (strcmp(path, "/") == 0) {
        fs_root = root;
    } else {
        // Find existing node and mark it
        // fs_node_t *target = vfs_walk(path);
        // if (target) { target->flags |= FS_MOUNTPOINT; target->ptr = root; }
    }

    return 0;
}

uint32_t read_fs(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    if (node->read != 0)
        return node->read(node, offset, size, buffer);
    else
        return 0;
}

uint32_t write_fs(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    if (node->write != 0)
        return node->write(node, offset, size, buffer);
    else
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

    if ((node->flags & 0x7) == FS_DIRECTORY && node->finddir != 0)
        return node->finddir(node, name);
    else
        return 0;
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

fs_node_t *vfs_lookup(fs_node_t *root, const char *path) {
    if (!root) return NULL;
    
    // Copy path to modify it
    char path_buf[128];
    // strncpy(path_buf, path, 127);
    int i=0;
    while(path[i] && i<127) { path_buf[i] = path[i]; i++; }
    path_buf[i] = 0;
    
    fs_node_t *current = root;
    char *p = path_buf;
    
    // Skip leading slash
    if (*p == '/') p++;
    
    while (*p) {
        char *slash = strchr(p, '/');
        if (slash) *slash = 0; // Terminate current segment
        
        fs_node_t *next = finddir_fs(current, p);
        if (!next) return NULL;
        
        current = next;
        
        if (slash) {
            p = slash + 1;
            while (*p == '/') p++; // Skip multiple slashes
        } else {
            break;
        }
    }
    
    return current;
}

