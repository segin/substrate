# Refactor Checklist: /dev/null Implementation

This document details the inspection of the original `pseudo.c` implementation and the steps taken to refactor it into a robust, compliant `null.c` driver.

## Inspection of Existing Implementation (`sys/drivers/devices/pseudo.c`)

| Feature | Requirement (EARS) | Status in `pseudo.c` | Issue / Gaps |
| :--- | :--- | :--- | :--- |
| **Write** | U1 (Discard, return count) | Implemented (`null_write`) | Correct, O(1). Used by `zero` too. |
| **Read** | U2 (Return 0/EOF) | Implemented (`null_read`) | Correct. |
| **Open** | U4 (Succeed) | Missing (Default) | Default VFS open succeeds, but explicit hook is better for robustness. |
| **Seek** | U3 (Succeed) | Missing (Default) | Relied on VFS default. Explicit comment needed for clarity. |
| **Poll** | E1 (POLLOUT only) | **Missing** | **CRITICAL**: VFS default `poll_fs` returns `POLLIN | POLLOUT` for char devices. This VIOLATES E1 (must not be readable). |
| **Ioctl** | U6 (ENOTTY) | **Missing** | **CRITICAL**: VFS default returns -1. While -1 is correct, errno handling should be explicit. |
| **Mmap** | O1 (EINVAL) | **Missing** | VFS default returns `(void*)-1`. Implicitly compliant but not explicit. |
| **Concurrency** | U8 (Safe) | Implicit | Safe due to lack of state. |
| **Efficiency** | C1 (No loops) | Implemented | `null_write` returned count directly. |

## Refactor Plan & Actions Taken

1.  **Split Implementation**: Moved `/dev/null` logic from `pseudo.c` to a dedicated `sys/drivers/devices/null.c` to separate concerns and allow for comprehensive implementation.
2.  **Fix Poll Behavior**: Implemented `null_poll` to explicitly return `POLLOUT` only. This fixes the violation where `select()`/`poll()` would report `POLLIN` (readable).
3.  **Explicit Ioctl**: Implemented `null_ioctl` to explicitly return `-ENOTTY` for all requests.
4.  **Explicit Mmap**: Implemented `null_mmap` to explicitly return failure (`(void*)-1`).
5.  **Explicit Open/Close**: Added `null_open` and `null_close` stubs for completeness.
6.  **Refactor `pseudo.c`**:
    - Renamed existing `null_write` to `discard_write` for use by `/dev/zero`.
    - Removed `null_read` and `null_node` from `pseudo.c`.
    - Called `null_init()` (from `null.c`) in `pseudo_init()`.

## Verification

The new `null.c` implementation satisfies all INCOSE EARS requirements, specifically correcting the `poll` behavior which was the main functional defect in the original implementation.
