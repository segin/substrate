# /dev/full - Always Full Device

The `/dev/full` device is a pseudo-device that always returns an `ENOSPC` (No space left on device) error when written to, and provides an infinite stream of zero bytes when read from (similar to `/dev/zero`).

## Implementation Details

- **Kernel Driver**: `sys/drivers/devices/full.c`
- **Major/Minor Number**: Typically mapped via `devfs`.
- **Behavior**:
    - **Read**: Returns `\0` (zero) bytes infinitely.
    - **Write**: Always returns `ENOSPC`.
    - **Seek**: Supports seeking (behavior varies by OS, but usually ignored or returned as success).

## Verification

### Regression Tests
- **Location**: `bin/test_full/`
- **Execution**: Run `/bin/test_full/test_full` on the target system to verify compliance.

## See Also
- `full(4)` man page: `man/man4/full.4`
- `/dev/zero`: `docs/specs/driver_null_zero.md`
