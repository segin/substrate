# /dev/zero Driver

This module implements the `/dev/zero` character device for Substrate.

## Build

To build the driver and tests:

```sh
# From kernel root
make
# Build userland tests
make -C bin
```

## Installation

The driver is statically linked into the kernel (`kernel.bin`). The device node `/dev/zero` is registered automatically at boot by `vfs_init` -> `pseudo_init` -> `zero_init`.

## Testing

Run the userland regression test suite:

```sh
# On the target system
/bin/test_zero/test_zero
```

Expected output should end with:
```
=== All Tests Passed ===
```

## Implementation Details

See `sys/drivers/devices/zero.c` for implementation.
See `REFACTOR.md` for design rationale and changes made.
