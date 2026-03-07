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
    - `/proc/cpuinfo`: CPU identification and capabilities.
    - `/proc/meminfo`: Global memory usage statistics.
    - `/proc/uptime`: System uptime in seconds.
    - `/proc/mounts`: Live view of the current kernel mount table.

## `/proc/mounts` Contract
- Data source: generated directly from the in-kernel `mountlist` at read time.
- Freshness: reflects mount, unmount, and remount activity immediately; it is not a cached snapshot.
- Record format: one line per mounted filesystem using the conventional six-field layout:
  - source
  - target
  - filesystem type
  - mount options
  - dump frequency
  - fsck pass number
- Current emitted options/defaults: Substrate currently emits `rw 0 0` for compatibility-oriented consumers.
- Field selection:
  - source prefers `f_mntfromname`; if empty, falls back to the filesystem type name.
  - target prefers `f_mntonname`; if empty, falls back to the internal mount path.
  - type prefers `f_fstypename`; if empty, falls back to `unknown`.
- Escaping: spaces, tabs, newlines, and backslashes inside emitted fields are escaped so shell tools and BusyBox-style parsers can consume the file safely.
- Scope: this interface is compatibility-oriented and intentionally text-based; typed mount control and enumeration remain the long-term stable ABI surface.

## Personality Awareness
ProcFS detects the personality of the calling process and adjusts the format of certain files accordingly. For example, `/proc/<pid>/status` returns a format compatible with the Linux kernel when accessed by a process running under the Linux personality.

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
