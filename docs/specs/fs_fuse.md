# FUSE (Filesystem in Userspace) Specification

## Overview
FUSE allows implementing filesystems in userspace by forwarding VFS requests from the kernel to a userspace daemon via a special character device (`/dev/fuse`).

## Implementation
- **Control Device:** `/dev/fuse` is used by the userspace daemon to receive kernel requests and send back responses.
- **Request Queue:** A circular buffer in the kernel stores pending VFS requests.
- **Protocol:** Uses a header-based protocol similar to Linux FUSE.
    - `fuse_in_header`: Describes the request from kernel.
    - `fuse_out_header`: Describes the response from userspace.
- **VFS Bridge:** VFS operations (e.g., `read`, `write`, `lookup`) are translated into FUSE requests and enqueued.

## API
### `void fuse_init(void)`
Registers the `/dev/fuse` device.

### `void fuse_fs_init(void)`
Registers the FUSE filesystem type with the VFS.

## Constraints
- Current implementation only supports a basic subset of opcodes.
- Blocking I/O: Kernel threads sleep while waiting for userspace responses.
- Single-daemon model for the prototype.
