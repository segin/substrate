#include <stdbool.h>
#include <stddef.h>
#include <sys/file.h>
#include <sys/fcntl.h>
#include <vfs/vfs.h>
#include <kern/sched.h>
#include <string.h>

extern int vfs_check_permissions(fs_node_t *node, uint32_t uid, uint32_t gid, int mode);
extern int vfs_may_open(fs_node_t *node, uint32_t uid, uint32_t gid, int flags);
extern int vfs_chmod_node(fs_node_t *node, uint32_t mode);

static int test_chmod_calls;
static uint32_t test_chmod_mode;
static int test_chmod_result;

static int mock_chmod(fs_node_t *node, uint32_t mode) {
    (void)node;
    test_chmod_calls++;
    test_chmod_mode = mode;
    return test_chmod_result;
}

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

    if (vfs_may_open(&node, 1000, 1000, 0) != 0) return false;
    if (vfs_may_open(&node, 1000, 1000, 1) != 0) return false;
    if (vfs_may_open(&node, 1001, 1001, 0) == 0) return false;
    if (vfs_may_open(&node, 1001, 1000, 0) != 0) return false;
    if (vfs_may_open(&node, 1001, 1000, 1) == 0) return false;
    if (vfs_may_open(&node, 1001, 1001, 0x0200 | 1) == 0) return false;

    return true;
}

bool test_vfs_chmod_updates_generic_node(void) {
    fs_node_t node;

    memset(&node, 0, sizeof(node));
    node.mask = 0644;
    node.ctime = 1234;

    if (vfs_chmod_node(&node, 0755) != 0) return false;
    if (node.mask != 0755) return false;
    if (node.ctime == 1234) return false;

    return true;
}

bool test_vfs_chmod_dispatches_callback(void) {
    fs_node_t node;

    memset(&node, 0, sizeof(node));
    node.mask = 0644;
    node.chmod = mock_chmod;
    test_chmod_calls = 0;
    test_chmod_mode = 0;
    test_chmod_result = 0;

    if (vfs_chmod_node(&node, 04755) != 0) return false;
    if (test_chmod_calls != 1) return false;
    if (test_chmod_mode != 04755) return false;
    if (node.mask != 04755) return false;

    return true;
}

bool test_vfs_chmod_rolls_back_on_callback_failure(void) {
    fs_node_t node;

    memset(&node, 0, sizeof(node));
    node.mask = 0644;
    node.ctime = 5678;
    node.chmod = mock_chmod;
    test_chmod_calls = 0;
    test_chmod_mode = 0;
    test_chmod_result = -5;

    if (vfs_chmod_node(&node, 0700) != -5) return false;
    if (test_chmod_calls != 1) return false;
    if (node.mask != 0644) return false;
    if (node.ctime != 5678) return false;

    return true;
}
