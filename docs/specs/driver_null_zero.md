# /dev/null and /dev/zero Specification

## Overview
These pseudo-devices provide standard Unix behavior for discarding input and generating null-byte streams.

## Implementation
- **`/dev/null`:**
    - **Read:** Always returns 0 (End of File).
    - **Write:** Discards all data and returns the number of bytes written (success).
- **`/dev/zero`:**
    - **Read:** Fills the provided buffer with null bytes (`0x00`) and returns the number of bytes requested.
    - **Write:** Discards all data (same as `/dev/null`).

## VFS Integration
- Both devices are registered as character devices (`FS_CHARDEVICE`) in DevFS.

## API
### `void null_init(void)`
Initializes and registers both devices with the system.

## Constraints
- No support for seek operations (always returns current position or 0).
- Memory-to-memory copy used for `/dev/zero` reads.
