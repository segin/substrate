# DevFS (Device Filesystem) Specification

## Overview
DevFS is a pseudo-filesystem that provides a view of all registered hardware devices in the `/dev` directory. It allows userspace to interact with drivers using standard VFS calls (`read`, `write`, `ioctl`).

## Implementation
- **Registry:** Drivers register their devices using `devfs_register_device()`, which adds an `fs_node_t` to a global list.
- **Namespace Policy:**
  - Root pseudo devices remain at `/dev/*` (e.g., `/dev/null`, `/dev/zero`).
  - Storage block devices are exposed under `/dev/storage/*`.
  - Raw disk providers remain visible as `/dev/storage/<disk>` (e.g., `/dev/storage/ide0`), with GEOM-derived partition nodes exposed alongside them (e.g., `/dev/storage/ide0p1`).
  - BSD disklabels additionally expose lettered slice nodes, with `c` reserved as the whole-container alias only when a BSD disklabel is present.
  - Communication character devices self-register under `/dev/comm/*` (e.g., `/dev/comm/serial0`, `/dev/comm/parallel0`).
  - USB character devices are reserved under `/dev/usb/busN/devM`.
  - Stable device aliases are exposed under `/dev/by-id/*` when a device model entry carries a serial string or GUID.
  - Nested device paths are accepted only under predeclared subsystem directories (namespace hardening against arbitrary roots like `/dev/notreal/*`).
- **Lifecycle:** Device-model managed nodes may be published through `device_publish()` and withdrawn through `device_unpublish()`, allowing add/remove lifecycle to drive devfs automatically for drivers that opt into the framework path.
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
