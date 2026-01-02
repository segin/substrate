# PS/2 Controller (i8042) Specification

## Overview
The PS/2 controller manages communication between the CPU and PS/2 input devices (Keyboard and Mouse).

## Initialization
1. **Disable Ports:** Sends `0xAD` and `0xA7` to the command port.
2. **Flush Buffer:** Reads from the data port as long as the status register's output buffer full bit is set.
3. **Configure:** Reads the configuration byte, disables interrupts and translation, and writes it back.
4. **Self-Test:** Sends `0xAA` and expects `0x55` in return.
5. **Enable Ports:** Sends `0xAE` to enable the first port (Keyboard).
6. **Enable Interrupts:** Enables IRQ1 in the configuration byte.

## API
### `void ps2_init(void)`
Performs full controller initialization.

### `uint8_t ps2_read_data(void)`
Waits for the output buffer to be full and reads a byte from the data port.

### `void ps2_write_command(uint8_t cmd)`
Waits for the input buffer to be empty and writes a command.

### `void ps2_write_data(uint8_t data)`
Waits for the input buffer to be empty and writes data.

## Constraints
- Current implementation only enables the first PS/2 port (Keyboard).
- Second port (Mouse) is initialized but left disabled.
- Polling used for initialization; interrupts used for runtime input.
