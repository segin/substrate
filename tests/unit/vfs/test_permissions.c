#include <stdbool.h>
#include <stddef.h>
#include <sys/file.h>
#include <vfs/vfs.h>
#include <kern/sched.h>
#include <string.h>

extern int vfs_check_permissions(fs_node_t *node, uint32_t uid, uint32_t gid, int mode);
extern int vfs_may_open(fs_node_t *node, uint32_t uid, uint32_t gid, int flags);

bool test_vfs_permissions_root(void) {
    fs_node_t node;
    node.uid = 1000;
    node.gid = 1000;
    node.mask = 0; // No permissions for anyone
    
    // Root should still have access
    if (vfs_check_permissions(&node, 0, 0, 7) != 0) return false;
    
    return true;
}

bool test_vfs_permissions_user(void) {
    fs_node_t node;
    node.uid = 1000;
    node.gid = 1000;
    node.mask = 0700; // rwx for owner
    
    if (vfs_check_permissions(&node, 1000, 1000, 7) != 0) return false;
    if (vfs_check_permissions(&node, 1001, 1000, 7) == 0) return false; // Group match but not UID
    
    return true;
}

bool test_vfs_permissions_group(void) {
    fs_node_t node;
    node.uid = 1000;
    node.gid = 1000;
    node.mask = 0070; // rwx for group
    
    if (vfs_check_permissions(&node, 1001, 1000, 7) != 0) return false;
    if (vfs_check_permissions(&node, 1001, 1001, 7) == 0) return false;
    
    return true;
}

bool test_vfs_open_permissions(void) {
    fs_node_t node;

    memset(&node, 0, sizeof(node));
    node.uid = 1000;
    node.gid = 1000;
    node.mask = 0640;

    if (vfs_may_open(&node, 1000, 1000, O_RDONLY) != 0) return false;
    if (vfs_may_open(&node, 1000, 1000, O_WRONLY) != 0) return false;
    if (vfs_may_open(&node, 1001, 1001, O_RDONLY) == 0) return false;
    if (vfs_may_open(&node, 1001, 1000, O_RDONLY) != 0) return false;
    if (vfs_may_open(&node, 1001, 1000, O_WRONLY) == 0) return false;
    if (vfs_may_open(&node, 1001, 1001, O_TRUNC | O_WRONLY) == 0) return false;

    return true;
}
