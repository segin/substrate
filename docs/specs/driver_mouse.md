# Mouse (PS/2) Driver Specification

## Overview
The mouse driver manages input from standard PS/2 mouse devices via the auxiliary PS/2 port.

## Initialization
1. **Enable Auxiliary Port:** Sends `0xA8` to the PS/2 command port.
2. **Enable Interrupts:** Sets bit 1 of the PS/2 configuration byte to enable IRQ12.
3. **Set Defaults:** Sends `0xF6` to the mouse.
4. **Enable Streaming:** Sends `0xF4` to the mouse to begin receiving data packets.

## Interrupt Handling
- **IRQ:** 12 (IDT vector 44).
- **Handler:** Reads raw bytes from the data port (0x60) and decodes 3-byte packets.

## Packet Decoding
- **Byte 0:** Button states (Left, Right, Middle) and sign bits for X/Y movement.
- **Byte 1:** X movement.
- **Byte 2:** Y movement.
- **Scaling:** Signed 8-bit values are accumulated into global `mouse_x` and `mouse_y` coordinates.

## Event Queue
- **Type:** Circular buffer.
- **Size:** 64 events.
- **Structure:** `mouse_event_t` contains `dx`, `dy`, and `buttons`.
- **Interrupt Safety:** The handler pushes events while the consumer pops them.

## API
### `void mouse_init(void)`
Performs mouse initialization and enables interrupts.

### `void mouse_handler(registers_t *regs)`
ISR for IRQ12. Processes raw mouse data, decodes packets, and pushes events to the queue.

### `void mouse_get_state(int32_t *x, int32_t *y, uint8_t *buttons)`
Returns the current accumulated position and button state.

### `int mouse_get_event(mouse_event_t *ev)`
Pops and returns the next event from the queue. Returns 1 on success, 0 if empty.

## Constraints
- Does not yet support 4-byte IntelliMouse packets (Scroll wheel).
- Accumulated coordinates are unbound (no screen resolution clipping).
