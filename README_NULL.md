# /dev/null Driver Implementation

This directory contains the implementation of the `/dev/null` character device driver for the Substrate kernel.

## Features

- **Standard Compliance**: Implements standard UNIX `/dev/null` semantics.
- **Robustness**: Explicit handling of all file operations (open, close, read, write, ioctl, poll, mmap).
- **Efficiency**: Zero-copy discard for write operations (O(1)).
- **Poll/Select**: Correctly reports `POLLOUT` only (never `POLLIN`).

## Files

- `sys/drivers/devices/null.c`: The kernel driver implementation.
- `sys/drivers/devices/null.h`: Header file exposing initialization.
- `man/man4/null.4`: BSD-style manual page.
- `bin/test_null/test_null.c`: Userland regression test suite.
- `REFACTOR.md`: Analysis of the refactor from the original implementation.

## Build Instructions

To build the kernel with the new driver:

```bash
make multiboot
```

To build the userland test utility:

```bash
make -C bin/test_null
```

(Note: `bin/test_null` is automatically built as part of `make all` or `make host_dist`).

## Running Tests

1.  Build the kernel image.
2.  Run the kernel in QEMU:
    ```bash
    ./run.sh
    ```
3.  Inside the QEMU shell, run the test utility:
    ```bash
    /bin/test_null
    ```

## Requirements Coverage (EARS)

- **U1 (Discard-writes)**: `null_write` returns full count immediately.
- **U2 (Read-EOF)**: `null_read` returns 0 immediately.
- **U3 (Seek)**: Supported via VFS (no-op).
- **U4 (Open)**: Always succeeds.
- **U5 (Attributes)**: Registered as `FS_CHARDEVICE`.
- **U6 (Ioctl)**: Returns `ENOTTY`.
- **E1 (Poll)**: Returns `POLLOUT` only.
- **O1 (Mmap)**: Returns failure.
- **C1 (Efficiency)**: No data copying or looping.

## Known Limitations

- **kqueue**: The kernel does not currently support `kqueue`, so `kqfilter` is not implemented.
