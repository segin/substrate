# /dev/kmem - Kernel Virtual Memory Driver

This patchset implements a robust, security-focused `/dev/kmem` driver for Substrate OS.
It provides controlled access to kernel virtual memory for privileged debugging and diagnostics.

## Features

- **Strict Privilege Enforcement**: Only `root` (euid 0) can access the device.
- **Security Policy**: Configurable read/write permissions via `sysctl`.
    - `kern.kmem_allow_read` (default: 1)
    - `kern.kmem_allow_write` (default: 0)
- **Securelevel Integration**: Respects `kern.securelevel`. If `securelevel > 0`, access is restricted.
- **Safe Memory Access**: Uses `copyin`/`copyout` with fault handling to prevent kernel panics on invalid access.
- **Explicit Denials**: `mmap()` and `ioctl()` are explicitly disallowed to reduce attack surface.
- **Test Harness**: Includes a kernel test helper module that exports a safe buffer for regression testing.

## Build Instructions

The driver is integrated into the kernel build system. To build the kernel:

```bash
make
```

To build the userland test suite:

```bash
make -C bin/kmem_test
```

## Installation

The kernel build produces `sys/kernel.bin`.
The userland test binary is `bin/kmem_test/test_kmem`.

To install to a distribution directory (e.g. for `make host_dist`):

```bash
make install
```

## Testing

### Prerequisites
- A running Substrate OS kernel (QEMU or hardware).
- Root privileges.

### Running Tests

1. Boot the system.
2. Run the test binary:

```bash
/bin/test_kmem
```

### Expected Output

```
Starting /dev/kmem tests...
Test Buffer: 0xC1234000 (Size: 4096)
PASS: Opened /dev/kmem
PASS: Read verification successful
PASS: Write denied by default policy
PASS: Enabled write policy
PASS: Write successful
PASS: Write verification successful
PASS: Read from invalid address returned EFAULT
PASS: mmap refused
All tests passed.
```

## Security Considerations

- **DO NOT ENABLE WRITES** on production systems. Keep `kern.kmem_allow_write` at 0.
- `securelevel` should be raised to at least 1 in production to prevent runtime modification of kernel memory even by root.
- The driver logs initialization and policy status to the kernel console.
