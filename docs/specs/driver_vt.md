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
- `Alt+F1..F12` switches the active VT.
- `Shift+PageUp` and `Shift+PageDown` enter/exit scrollback and adjust the active VT scrollback view.
- when scrollback is active, the VGA backend redraws historical lines and hides the hardware cursor.

## Non-Goals
- framebuffer console composition
- full VT102 feature parity
- hardware smooth-scroll via CRTC start-address programming
