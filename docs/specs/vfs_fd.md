# VFS File Descriptor Management Specification

## Overview
TestUnix uses a reference-counted model for managing file descriptors (FDs). This allows multiple descriptors to point to the same open file structure, supporting operations like `dup()` and `dup2()`.

## Implementation
- **File Structure (`file_t`):**
    - `node`: Pointer to the VFS node (vnode).
    - `offset`: Current read/write position.
    - `flags`: Open flags (e.g., `O_RDONLY`).
    - `ref_count`: Number of FDs referencing this structure.
- **FD Table:** Each process maintains an array of `file_t` pointers.
- **Reference Counting:**
    - `sys_open()`: Allocates a new `file_t`, sets `ref_count = 1`.
    - `sys_dup()` / `sys_dup2()`: Increments `ref_count` of the existing `file_t`.
    - `sys_close()`: Decrements `ref_count`. If it hits 0, the `file_t` is freed and the underlying filesystem's `close` function is called.

## API
### `int sys_dup(int oldfd)`
Duplicates an existing file descriptor.

### `int sys_dup2(int oldfd, int newfd)`
Duplicates a file descriptor to a specific descriptor number, closing the target if it was already open.

## Constraints
- Fixed-size FD table per process (currently 32).
- No global file table; all `file_t` allocations are currently from a fixed pool.
