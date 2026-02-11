# Refactoring /dev/full

This document details the refactoring of `/dev/full` from a pseudo-device stub to a fully compliant character device driver.

## Checklist

- [x] **Behavior parity**: Verify that all write paths return `-ENOSPC`.
  - Implemented in `full_write`.
- [x] **Read correctness**: Ensure reads return zero bytes.
  - Implemented in `full_read` using `memset`.
- [x] **Seek**: Confirm `lseek` always succeeds.
  - Handled by VFS default seek logic (offset update), consistent with infinite file.
- [x] **Ioctl**: Ensure unsupported ioctls return `ENOTTY`.
  - Implemented `full_ioctl` returning `-ENOTTY`.
- [x] **poll/kqueue**: Audit `poll`/`kqueue` support.
  - Implemented `full_poll` returning `POLLIN | POLLOUT`.
- [x] **mmap**: Make explicit decision (O1a).
  - Implemented `full_mmap` returning `-1` (explicit disallow).
- [x] **Testing**: Add userland test-suite.
  - Created `bin/test_full/test_full.c`.
- [x] **Documentation**: Add man(4) page.
  - Updated `man/man4/full.4`.
- [x] **Performance**: Efficient reads.
  - `memset` used for kernel buffer filling.
- [x] **Concurrency**: Stateless implementation.
  - No mutable state in `full.c`.

## Requirements Matrix (EARS)

| Requirement | Description | Implementation | Test Case |
| :--- | :--- | :--- | :--- |
| **U1** | Always-fail-writes | `full_write` returns `-ENOSPC` | `test_write_enospc` |
| **U2** | Read-zeroes | `full_read` fills buffer with 0 | `test_read_zero` |
| **U3** | Seek semantics | VFS handles offset; read ignores offset | `test_seek` |
| **U4** | Open semantics | `devfs` allows open | Implicit (all tests open) |
| **U5** | File attributes | `S_IFCHR`, Major 1, Minor 7 | `man/man4/full.4` |
| **U6** | Ioctl | `full_ioctl` returns `-ENOTTY` | `test_ioctl` |
| **U7** | Nonblocking | Non-blocking by design | N/A (same behavior) |
| **U8** | Concurrency | Stateless | `test_concurrency` |
| **E1** | Poll/select | `full_poll` returns `IN|OUT` | `test_poll` |
| **UB1** | No partial writes | `full_write` returns full error | `test_write_enospc` |
| **O1a** | Disallow mmap | `full_mmap` returns failure | `test_mmap` |

## Design Notes

- **Separation**: The `full` driver logic was moved from `sys/drivers/devices/pseudo.c` to `sys/drivers/devices/full.c` to provide a clean, dedicated implementation.
- **Poll Semantics**: `POLLOUT` is set because writing is "ready" (non-blocking), even though it results in an error. This aligns with standard behavior where `select()` reports writable for `/dev/full` but writes fail.
- **Mmap**: Explicitly disabled to avoid ambiguity. Anonymous mapping with zero-fill is better served by `/dev/zero`.

<hr>

# Refactor Checklist for /dev/kmem

This checklist outlines steps to audit and improve existing /dev/kmem implementations
to meet security and robustness requirements.

## 1. Privilege & Lockdown
- [ ] Verify that `open()`, `read()`, `write()`, and `ioctl()` explicitly check for privileged access (euid == 0).
- [ ] Verify that `securelevel` or kernel lockdown mode is checked. If `securelevel > 0`, access should be denied or restricted to read-only.
- [ ] Ensure that `mmap()` support is removed or explicitly returns `EPERM`/`EINVAL`.

## 2. Address Interpretation
- [ ] Confirm file offsets are treated as **Kernel Virtual Addresses (KVA)**, not physical addresses.
- [ ] For physical memory access, use `/dev/mem`, not `/dev/kmem`.

## 3. Safe Copy Primitives
- [ ] Replace direct pointer dereferences (e.g., `*ptr = val`) with safe kernel copy primitives (`copyin()`, `copyout()`, `uiomove()`).
- [ ] Ensure fault handlers are active during copy operations to catch page faults (invalid addresses) gracefully.
- [ ] Verify that faults return appropriate error codes (`EFAULT`) and do not panic the kernel.

## 4. Policy Enforcement
- [ ] Implement sysctl controls for enabling/disabling read and write access (`kern.kmem.allow_read`, `kern.kmem.allow_write`).
- [ ] Ensure default policy is conservative (Write Disabled).
- [ ] Consider implementing allow/deny lists for sensitive kernel regions (text, page tables, etc.).

## 5. Concurrency & Reentrancy
- [ ] Ensure the driver handles concurrent access correctly.
- [ ] Verify that per-file offsets are maintained correctly (use `struct file` or `uio` offset, not global static offset).

## 6. Testing Strategy
- [ ] Create a safe kernel test helper module that exports a specific buffer address.
- [ ] Verify regression tests run against this safe buffer only.
- [ ] Ensure tests cover:
    - Privilege escalation attempts (non-root access).
    - Policy enforcement (write when disabled).
    - Fault handling (read/write invalid addresses).
    - `mmap` rejection.

## 7. Audit & Logging
- [ ] Log privileged access attempts to system audit logs.
- [ ] Log policy changes.

## 8. Documentation
- [ ] Update `man(4)` page to reflect security policies and limitations.
- [ ] Document the risk of using `/dev/kmem` on production systems.
---

# Refactor Report: /dev/zero Implementation

## Overview
This refactor replaces the minimal `/dev/zero` implementation in `sys/drivers/devices/pseudo.c` with a dedicated, fully-featured driver in `sys/drivers/devices/zero.c`. The new implementation adheres to BSD semantics and supports `mmap`, `poll`, and large reads efficiently.

## Changes

### 1. New Driver: `sys/drivers/devices/zero.c`
- **Read**: Implemented efficient zero-filling using `memset`. Returns requested size (infinite stream).
- **Write**: Implemented discard logic that returns success (byte count).
- **Mmap**: Added support for mapping zero-filled pages.
    - **Implementation Note**: Due to the current kernel's `sys_mmap` implementation (which expects eager mapping from device drivers and lacks full demand-paging hooks for devices), `zero_mmap` eagerly allocates and maps pages using `pmm_alloc_block` and `pmap_enter`. This ensures functional correctness (zero-filled memory) but does not yet leverage copy-on-write optimizations for large mappings. Future work on `sys_mmap` and `vm_fault` integration is required for true demand paging.
- **Poll**: Returns `POLLIN | POLLOUT` immediately.
- **Ioctl**: Returns `-ENOTTY` for all requests.

### 2. Modification of `sys/drivers/devices/pseudo.c`
- Removed the static `zero_node` and its initialization.
- Renamed the internal `zero_read` function to `full_read_zeros` to clarify its use by `/dev/full` (which mimics zero behavior on read).
- Added a call to `zero_init()` in `pseudo_init()` to register the new driver.

### 3. Userland Tests: `bin/test_zero/`
- Added a comprehensive regression test suite `test_zero.c` covering:
    - Read (small/large buffers)
    - Write (discard verification)
    - Seek (offset handling)
    - Poll (readiness)
    - Mmap (read/write verification)
    - Ioctl (error handling)
    - Concurrency (fork test)
- Updated `bin/Makefile` to include `test_zero` in the build.

## Verification Checklist
- [x] **U1 Read-zeroes**: `test_read` verifies zero content.
- [x] **U2 Discard-writes**: `test_write` verifies success return.
- [x] **U3 Seek**: `test_seek` verifies `lseek` success.
- [x] **U4 Open**: `test_open_stat` verifies open modes.
- [x] **U5 Stat**: `test_open_stat` verifies `S_IFCHR`.
- [x] **U6 Ioctl**: `test_ioctl` verifies `ENOTTY`.
- [x] **E1 Poll**: `test_poll` verifies `POLLIN | POLLOUT`.
- [x] **O1 Mmap**: `test_mmap` verifies mapping functionality.

## Future Work
- Refactor `sys_mmap` to support lazy mapping for devices (returning a VM object instead of eager mapping).
- Implement COW zero page sharing for read-only mappings of `/dev/zero`.
