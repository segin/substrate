# Input Subsystem Specification

## Overview
The input subsystem provides a unified abstraction for all input devices (keyboards, mice, etc.). It uses an event-based model similar to the Linux `evdev` interface.

## Input Events
- **Structure (`input_event_t`):**
    - `uint16_t type`: Event type (`EV_KEY`, `EV_REL`, `EV_ABS`).
    - `uint16_t code`: Key scancode or relative axis index.
    - `int32_t value`: Key state (0/1) or movement amount.
- **Event Types:**
    - `EV_KEY`: Keyboard buttons or mouse buttons.
    - `EV_REL`: Relative axis movement (Mouse).
    - `EV_ABS`: Absolute axis movement (Touchscreen).

## Global Input Queue
- All input drivers push events to a single global queue in the kernel.
- **Size:** 128 events.
- **Blocking:** Reading from `/dev/input` blocks if no events are available.

## VFS Interface
- **Node:** `/dev/input` (mapped to `input_device_node`).
- **Read:** Returns an array of `input_event_t` structures.

## API
### `void input_enqueue(uint16_t type, uint16_t code, int32_t value)`
Kernel-side API for drivers to submit events.

### `void input_init(void)`
Initializes the input subsystem and the VFS node.

## Constraints
- Single queue for all devices (no per-device separation yet).
- No timestamping of events.
- `input_init` must be called after VFS initialization.
