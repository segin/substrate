# PS/2 Controller Specification

## Scope
`sys/drivers/input/ps2.c` implements the i8042-compatible controller setup and
controller-side I/O primitives for keyboard and auxiliary PS/2 devices.

## I/O Primitives
- `ps2_wait_write()`: wait until the input buffer is empty.
- `ps2_wait_read()`: wait until the output buffer is full.
- `ps2_write_command(cmd)`: write a controller command to port `0x64`.
- `ps2_write_data(data)`: write a data byte to port `0x60`.
- `ps2_read_data()`: read a byte from port `0x60`, returning `0xFF` on timeout.
- `ps2_read_data_timeout(data, loop_count)`: bounded read helper.
- `ps2_write_aux(data)`: send a byte to port 2 through the `0xD4` prefix.

## Initialization Sequence
`ps2_init()` performs:
1. Disable port 1 and port 2.
2. Flush stale output-buffer bytes.
3. Read the configuration byte, clear IRQ-enable bits, set the system flag,
   and enable translation.
4. Run the controller self-test, retrying once before failing.
5. Re-write the configuration byte after self-test.
6. Probe dual-channel capability by enabling port 2 and re-reading the config.
7. Run interface tests for port 1 and, if present, port 2.
8. Re-enable working ports and restore IRQ delivery.
9. Reset the auxiliary device and enable data reporting when port 2 is present.

## Error Handling
- Port 1 test failure is fatal.
- Port 2 test failure is logged and degrades the driver to single-channel mode.
- Mouse reset and enable are best-effort and do not abort controller bring-up.

## Timeouts
- `PS2_TIMEOUT_LOOPS`: general controller polling bound.
- `PS2_MOUSE_TIMEOUT_LOOPS`: longer timeout for auxiliary reset/BAT responses.
