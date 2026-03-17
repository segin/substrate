# Virtual Terminal Driver

## Scope
- manages `vt0..vt11`
- stores per-VT text buffer, cursor state, ANSI parser state, and scrollback
- coordinates active-VT redraw through the VGA text backend

## Current Contract
- each VT owns a full text buffer sized to the current physical geometry.
- the hardware text console treats the last physical text row as a kernel-owned status line rendered black-on-white; the usable tty geometry reported to userland excludes that row (e.g., `80x24` on an `80x25` mode).
- a dedicated kernel `vtstatus` thread refreshes the status line once per second, showing the active VT number and wall-clock time in ISO 8601 UTC form.
- each VT owns a fixed scrollback ring of `256` lines at the maximum supported width.
- `Alt+F1..F12` switches the active VT.
- `Shift+PageUp` and `Shift+PageDown` enter/exit scrollback and adjust the active VT scrollback view.
- when scrollback is active, the VGA backend redraws historical lines and hides the hardware cursor.

## Non-Goals
- framebuffer console composition
- full VT102 feature parity
- hardware smooth-scroll via CRTC start-address programming
