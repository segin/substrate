# VFS Chroot Specification

## Overview
TestUnix supports per-process root directories via the `chroot()` system call. This allows a process and its children to see a specific subdirectory as the root of the filesystem.

## Implementation
- **Process State:** The `process_t` structure includes a `root_node` field (pointer to a `fs_node_t`).
- **Initialization:**
    - The initial kernel process sets `root_node` to the global `fs_root`.
    - New processes created via `fork()` or `sched_create_process()` inherit the parent's `root_node`.
- **Path Resolution:** 
    - When a system call encounters a path starting with `/`, it uses `current_process->root_node` as the starting point for traversal instead of the global `fs_root`.
    - Relative paths still start from the current working directory (to be implemented).

## API
### `int sys_chroot(const char *path)`
Changes the root directory of the calling process to the specified path.

## Constraints
- Does not automatically change the current working directory (CWD).
- No mechanism to "break out" of a chroot jail once entered.
- Only directories can be set as root.
