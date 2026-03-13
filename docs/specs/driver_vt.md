# Virtual Terminal Driver

## Scope
- manages `vt0..vt11`
- stores per-VT text buffer, cursor state, ANSI parser state, and scrollback
- coordinates active-VT redraw through the VGA text backend

## Current Contract
- each VT owns a full text buffer sized to the current physical geometry
- each VT owns a fixed scrollback ring of `256` lines at the maximum supported width
- `vt_activate(n)` switches the active VT, repoints `/dev/console` input to that VT's `tty`, and requests a backend redraw
- `Shift+PageUp` and `Shift+PageDown` adjust the active VT scrollback view by one visible page
- when scrollback is active, the VGA backend redraws historical lines and hides the hardware cursor

## Non-Goals
- framebuffer console composition
- full VT102 feature parity
- hardware smooth-scroll via CRTC start-address programming
