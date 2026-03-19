# Virtual Terminal Driver

## Scope
- manages `vt0..vt11`
- stores per-VT text buffer, cursor state, ANSI parser state, and scrollback
- coordinates active-VT redraw through the VGA text backend

## Current Contract
- each VT owns a full text buffer sized to the current physical geometry.
- each VT owns an ANSI/VT102 parser state machine, and the active hardware-text backend feeds printable characters and escape sequences through the shared `ansi_handler` callbacks.
- the hardware text console treats the last physical text row as a kernel-owned status line rendered black-on-white; the usable tty geometry reported to userland excludes that row (e.g., `80x24` on an `80x25` mode).
- a dedicated kernel `vtstatus` thread refreshes the status line once per second, showing the active VT number and wall-clock time in ISO 8601 UTC form.
- each VT owns a fixed scrollback ring of `256` lines at the maximum supported width.
- the hardware-text VT backend honors DECSTBM scroll regions through the shared ANSI callback table, so line insert/delete, index, reverse-index, and bulk scroll operations stay clipped to the configured top/bottom margins.
- the hardware-text attribute byte is treated as a 4-bit foreground plus 4-bit background palette, so ANSI SGR color changes can address the full 16 VGA text colors for both foreground and background.
- the hardware-text backend exposes a bulk write path for both console logging and `/dev/ttyN` output, so multi-byte writes traverse the same ANSI parser and cursor-update logic as single-character output.
- each VT carries its own configurable tab width, defaulting to `8` columns, and the hardware-text backend advances horizontal tab stops against that per-console setting.
- the VGA text `tty_driver->ioctl()` path exposes backend-specific controls for per-VT tab width, cursor visibility, cursor blink, and text blink mode, separate from generic termios handling.
- the VGA text backend advances cursor blink from the real timer tick path and keeps blink state per VT, so a steady cursor and a blinking cursor are both supported without reusing the text-attribute blink bit.
- the VGA text backend programs the attribute-controller mode register so blink mode can be toggled on and off, allowing bright backgrounds when blink is disabled.
- `Alt+F1..F12` switches the active VT.
- `Shift+PageUp` and `Shift+PageDown` enter/exit scrollback and adjust the active VT scrollback view.
- when scrollback is active, the VGA backend redraws historical lines and hides the hardware cursor.

## Non-Goals
- framebuffer console composition
- full VT102 feature parity
- hardware smooth-scroll via CRTC start-address programming
