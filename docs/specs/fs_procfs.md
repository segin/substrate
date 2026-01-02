# ProcFS (Process Filesystem) Specification

## Overview
ProcFS is a virtual filesystem that provides an interface to kernel process structures. it is typically mounted at `/proc` and allows userspace utilities like `ps` and `top` to retrieve process information.

## Implementation
- **Dynamic Content:** Files in `/proc` do not exist on disk; their content is generated on-the-fly by the kernel when `read()` is called.
- **Hierarchy:**
    - `/proc/`: Root directory containing a subdirectory for each active PID.
    - `/proc/<pid>/`: Directory for a specific process.
    - `/proc/<pid>/status`: Contains basic process metadata (Name, PID, UID, GID).
    - `/proc/<pid>/cmdline`: Contains the command line arguments (stubbed).
    - `/proc/<pid>/maps`: Contains the virtual memory layout (stubbed).

## VFS Integration
- Registered as a virtual filesystem in the VFS registry.
- Uses `inode` field in `fs_node_t` to store the target PID for per-process files.

## API
### `void procfs_init(void)`
Initializes the ProcFS driver and registers it with the VFS.

## Constraints
- Currently supports a maximum of 16 processes (matching `MAX_PROCS`).
- `cmdline` and `maps` are initialized but return empty/stubbed data.
- Read operations are limited to 256 bytes per call in the initial prototype.
