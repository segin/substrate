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
- **Handler:** Reads raw bytes from the data port (0x60).

## API
### `void mouse_init(void)`
Performs mouse initialization and enables interrupts.

### `void mouse_handler(registers_t *regs)`
ISR for IRQ12. Processes raw mouse data.

## Constraints
- Packet parsing (decoding movement and buttons) is not yet implemented.
- Polling used for initial communication; interrupts used for data.
