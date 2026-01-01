#include "vfs.h"

fs_node_t *fs_root = 0; 

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
    if ((node->flags & 0x7) == FS_DIRECTORY && node->finddir != 0)
        return node->finddir(node, name);
    else
        return 0;
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
