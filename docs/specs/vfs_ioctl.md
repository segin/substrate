# sys_ioctl Framework Specification

## Overview
The `ioctl` (Input/Output Control) system call provides a mechanism for userspace to perform device-specific operations that do not fit into the standard `read`/`write` model.

## Implementation
- **VFS Integration:** The `fs_node_t` structure includes an `ioctl` function pointer.
- **Dispatch:** `sys_ioctl` looks up the file descriptor, identifies the target VFS node, and calls its `ioctl` handler if defined.
- **Error Handling:** Returns `ENOTTY` (-1) if the node does not support `ioctl` or if the handler is NULL.

## API
### `int sys_ioctl(int fd, int request, void *arg)`
Primary system call for device control.
- `fd`: File descriptor of the device.
- `request`: Device-specific command code.
- `arg`: Pointer to command-specific data.

## Constraints
- Argument size and type are command-specific and not validated by the generic framework.
- No support for `copy_from_user` validation in the initial prototype.
