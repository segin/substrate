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
