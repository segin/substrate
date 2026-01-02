# VFS Mount and Registration Specification

## Overview
The VFS supports multiple filesystem types and allows them to be mounted into a unified directory tree.

## Filesystem Registration
- Filesystem drivers register themselves using `vfs_register_filesystem()`.
- **Structure (`filesystem_t`):**
    - `name`: Unique name of the filesystem type (e.g., "ext2", "fat").
    - `mount`: Function pointer to the implementation's mount logic.

## Mounting
- `vfs_mount()` binds a device and filesystem type to a specific path in the VFS tree.
- If the path is `/`, it sets the global `fs_root`.
- Otherwise, it marks the target node as `FS_MOUNTPOINT` and stores the new root in the `ptr` field.

## Crossing Mount Points
- When traversing the VFS tree (e.g., in `finddir_fs()`), if a node with the `FS_MOUNTPOINT` flag is encountered, the traversal automatically continues into the mounted filesystem's root.

## API
### `void vfs_register_filesystem(filesystem_t *fs)`
Adds a filesystem type to the global registry.

### `int vfs_mount(const char *device, const char *path, const char *type, uint32_t flags, void *data)`
Mounts a device at the specified path.

## Constraints
- Unmounting is not yet implemented.
- Recursive path walking (e.g., `vfs_walk("/usr/bin")`) is not yet fully generalized.
- Only one mount per path is supported.
