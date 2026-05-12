#include <stdbool.h>
#include <stddef.h>
#include <sys/file.h>
#include <sys/fcntl.h>
#include <vfs/vfs.h>
#include <kern/sched.h>
#include <string.h>

extern int vfs_check_permissions(fs_node_t *node, uint32_t uid, uint32_t gid, int mode);
extern int vfs_check_permissions_groups(fs_node_t *node, uint32_t uid, uint32_t gid,
                                        const uint32_t *groups, int ngroups, int mode);
extern int vfs_may_open(fs_node_t *node, uint32_t uid, uint32_t gid, int flags);
extern int vfs_may_open_groups(fs_node_t *node, uint32_t uid, uint32_t gid,
                               const uint32_t *groups, int ngroups, int flags);
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

    memset(&node, 0, sizeof(node));
    node.uid = 1000;
    node.gid = 1000;
    node.mask = 0; /* No permissions for anyone */

    if (vfs_check_permissions(&node, 0, 0, 4) != 0) return false;
    if (vfs_check_permissions(&node, 0, 0, 2) != 0) return false;
    if (vfs_check_permissions(&node, 0, 0, 1) == 0) return false;

    node.mask = 0100;
    if (vfs_check_permissions(&node, 0, 0, 1) != 0) return false;

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

/*
 * Supplementary-group access checks.  File owned by uid=1000, gid=2000,
 * mode 0070 (group rwx only).  A user whose primary gid does not match
 * 2000 but who has 2000 in their supplementary list should still get
 * access via the group class.
 */
bool test_vfs_permissions_supp_group_match(void) {
    fs_node_t node;
    memset(&node, 0, sizeof(node));
    node.uid = 1000;
    node.gid = 2000;
    node.mask = 0070;

    /* Without supplementary groups, user 1001/1001 falls to "other"
     * which has no bits — must deny. */
    if (vfs_check_permissions_groups(&node, 1001, 1001, NULL, 0, 4) == 0)
        return false;

    /* Primary gid 1001 but 2000 in supplementary list — must allow. */
    uint32_t supp[] = { 50, 2000, 999 };
    if (vfs_check_permissions_groups(&node, 1001, 1001, supp, 3, 7) != 0)
        return false;

    /* Supplementary list without 2000 — must still deny. */
    uint32_t supp_no[] = { 50, 999 };
    if (vfs_check_permissions_groups(&node, 1001, 1001, supp_no, 2, 4) == 0)
        return false;

    return true;
}

/*
 * POSIX tiered selection: if uid matches owner, only owner bits are
 * consulted — even when the file's gid is in the caller's groups and
 * the group class would have allowed.  Supplementary groups must not
 * leak a permission that the owner class doesn't grant.
 */
bool test_vfs_permissions_owner_class_is_exclusive(void) {
    fs_node_t node;
    memset(&node, 0, sizeof(node));
    node.uid  = 1000;
    node.gid  = 2000;
    node.mask = 0070;       /* owner has nothing, group has rwx */

    uint32_t supp[] = { 2000 };
    /* uid matches owner ⇒ stuck in owner class ⇒ deny, even though
     * group class would allow. */
    if (vfs_check_permissions_groups(&node, 1000, 99, supp, 1, 4) == 0)
        return false;

    return true;
}

/*
 * vfs_may_open_groups must thread groups into the underlying check.
 */
bool test_vfs_may_open_supp_groups(void) {
    fs_node_t node;
    memset(&node, 0, sizeof(node));
    node.uid  = 1000;
    node.gid  = 2000;
    node.mask = 0060;       /* group rw, no execute */

    uint32_t supp[] = { 100, 2000 };
    /* O_RDONLY should pass via supplementary group membership. */
    if (vfs_may_open_groups(&node, 1001, 1001, supp, 2, 0) != 0) return false;
    /* O_WRONLY should pass too. */
    if (vfs_may_open_groups(&node, 1001, 1001, supp, 2, 1) != 0) return false;
    /* Caller without 2000 in any of their groups falls to "other" — deny. */
    if (vfs_may_open_groups(&node, 1001, 1001, NULL, 0, 0) == 0) return false;
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
