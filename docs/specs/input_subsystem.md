# Input Subsystem Specification

## Overview
The input subsystem provides a unified abstraction for input devices
(keyboards, mice, and similar sources). It uses an event stream broadly similar
to Linux `evdev`, but currently exposes a single global queue as
`/dev/input/event0`.

## Input Events
- **Structure (`input_event_t`):**
    - `uint64_t time_sec`: event timestamp seconds field.
    - `uint64_t time_usec`: event timestamp microseconds field.
    - `uint16_t type`: Event type (`EV_KEY`, `EV_REL`, `EV_ABS`, `EV_SYN`).
    - `uint16_t code`: Key code or relative axis index.
    - `int32_t value`: Key state (0/1) or movement amount.
- **Event Types:**
    - `EV_KEY`: Keyboard buttons or mouse buttons.
    - `EV_REL`: Relative axis movement (Mouse).
    - `EV_ABS`: Absolute axis movement (Touchscreen).
- **Mouse button codes:**
    - `BTN_LEFT` = `0x110`
    - `BTN_RIGHT` = `0x111`
    - `BTN_MIDDLE` = `0x112`

## Global Event Queue
- All input drivers push events to a single global queue in the kernel.
- **Size:** 64 events.
- **Blocking:** Reading from `/dev/input/event0` blocks if no events are
  available.
- **Overflow:** readers that fall behind the fixed queue receive an overrun
  error from the read path.

## VFS Interface
- **Node:** `/dev/input/event0`.
- **Read:** Returns an array of `input_event_t` structures.

## API
### `int input_register_device(input_dev_t *dev)`
Registers an input producer in the global device list.

### `void input_unregister_device(input_dev_t *dev)`
Removes an input producer from the global device list.

### `void input_report_event(input_dev_t *dev, uint16_t type, uint16_t code, int32_t value)`
Kernel-side API for drivers to submit an input event.

### `void input_report_key(input_dev_t *dev, uint16_t code, int32_t value)`
Convenience wrapper for `EV_KEY` events.

### `void input_report_rel(input_dev_t *dev, uint16_t code, int32_t value)`
Convenience wrapper for `EV_REL` events.

### `void input_report_abs(input_dev_t *dev, uint16_t code, int32_t value)`
Convenience wrapper for `EV_ABS` events.

### `void input_sync(input_dev_t *dev)`
Emits an `EV_SYN` event boundary after a batch of driver-generated events.

### `void input_enqueue(uint16_t type, uint16_t code, int32_t value)`
Legacy compatibility wrapper for direct event injection.

### `void input_init(void)`
Initializes the input subsystem and the VFS node.

## Constraints
- Single queue for all devices (no per-device separation yet).
- Device identity is not yet surfaced in the event payload.
- `input_init` must be called after VFS initialization.
