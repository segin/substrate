# VFS Permissions Specification

## Overview
Substrate implements a standard Unix permissions model based on UIDs, GIDs, and permission masks (rwxrwxrwx).

## Implementation
- **Permission Check:** The `vfs_check_permissions()` function evaluates if a given UID/GID pair has the required access mode (`R_OK`, `W_OK`, `X_OK`) for a specific VFS node.
- **Rules:**
    1. **Root (UID 0):** Always granted full access regardless of the node's mask.
    2. **Owner:** If the caller's UID matches `node->uid`, the owner bits of the mask are checked.
    3. **Group:** If the caller's GID matches `node->gid`, the group bits of the mask are checked.
    4. **Others:** Otherwise, the "others" bits of the mask are checked.

## API
### `int vfs_check_permissions(fs_node_t *node, uint32_t uid, uint32_t gid, int mode)`
Core kernel-side function for permission evaluation.

### `int sys_access(const char *path, int mode)`
Userspace system call to check accessibility of a file.

## Constraints
- Supplementary groups are not yet supported.
- ACLs are not supported.
- Root bypass is hardcoded.
