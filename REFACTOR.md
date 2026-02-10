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
