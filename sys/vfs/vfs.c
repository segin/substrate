#include "vfs.h"
#include <string.h>

fs_node_t *fs_root = 0; 

static filesystem_t *filesystems = NULL;

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

// Temporary Mock Root for Init testing
static struct dirent mock_dirent;
static fs_node_t mock_root;
static fs_node_t mock_init;

struct dirent *mock_readdir(fs_node_t *node, uint32_t index) {
    (void)node;
    if (index == 0) {
        // Return 'init'
        int i=0;
        char *n = "init";
        while(n[i]) { mock_dirent.name[i] = n[i]; i++; }
        mock_dirent.name[i] = 0;
        mock_dirent.ino = 2;
        return &mock_dirent;
    }
    return 0;
}

fs_node_t *mock_finddir(fs_node_t *node, char *name) {
    (void)node;
    // Check if name is "init" or "sbin/init" logic (vfs handles / splitting usually, but here we get one component)
    // Actually finddir_fs in basic VFS usually gets one component.
    // But our try_init passes "sbin/init".
    // Our finddir_fs logic in syscall/main is: `path + 1`.
    // If path is "/sbin/init", it asks for "sbin/init".
    // Standard VFS walk is component by component.
    // For this hack, we check full match.
    
    // Simple check
    int i=0;
    while(name[i] && name[i] != '/') i++;
    
    // If user asked for "sbin/init", we cheat and say yes.
    // In reality, we should mount a ramdisk or something.
    
    return &mock_init;
}

void vfs_init_mock_root(void) {
    mock_root.flags = FS_DIRECTORY;
    mock_root.readdir = mock_readdir;
    mock_root.finddir = mock_finddir;
    
    mock_init.flags = FS_FILE;
    
    fs_root = &mock_root;
}
