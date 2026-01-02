# DevFS (Device Filesystem) Specification

## Overview
DevFS is a pseudo-filesystem that provides a view of all registered hardware devices in the `/dev` directory. It allows userspace to interact with drivers using standard VFS calls (`read`, `write`, `ioctl`).

## Implementation
- **Registry:** Drivers register their devices using `devfs_register_device()`, which adds an `fs_node_t` to a global list.
- **Lookup:** The `devfs_finddir()` function searches the list of registered devices by name.
- **Enumeration:** `devfs_readdir()` allows listing all devices in `/dev`.
- **Mounting:** DevFS is typically mounted at `/dev` during kernel initialization.

## API
### `void devfs_init(void)`
Initializes the DevFS driver and registers it with the VFS.

### `void devfs_register_device(fs_node_t *node)`
Adds a driver-provided VFS node to the DevFS registry.

## Constraints
- Fixed maximum number of devices (currently 64).
- No support for nested directories in `/dev` yet.
- Only character and block devices are typically registered.
