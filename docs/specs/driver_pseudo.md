# Pseudo-Devices Specification

## Overview
Pseudo-devices are software-only drivers that provide standard Unix interfaces for common data streams and sinks.

## Implementation
- **`/dev/null`:** Discards all writes, returns EOF on reads.
- **`/dev/zero`:** Discards all writes, returns null bytes on reads.
- **`/dev/full`:** Returns zeros on reads, returns error (ENOSPC) on writes.
- **`/dev/random`:** Returns a stream of pseudo-random bytes based on a simple PRNG.

## VFS Integration
- Registered as character devices in DevFS.

## API
### `void pseudo_init(void)`
Initializes and registers all four pseudo-devices.

## Constraints
- `/dev/random` uses a basic linear congruential generator (not cryptographically secure).
- `/dev/full` currently simulates ENOSPC by returning 0 bytes written (logic to return `-ENOSPC` requires syscall wrapper adjustment).
