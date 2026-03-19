# Keyboard Driver Specification

## Scope
`sys/drivers/input/keyboard.c` implements the IRQ1 handler, Set 1 scancode
decoding, modifier tracking, ASCII translation, and handoff into the VT/TTY and
input-event paths.

## IRQ Handling
- `keyboard_handler()` reads the raw scancode from port `0x60`.
- It harvests entropy from the cycle counter plus the scancode value.
- It treats `0xE0` as an extended-prefix latch for the next scancode.
- PIC EOI is handled by the common IRQ dispatcher, not by the driver.

## Modifier State
- Left/right modifiers are tracked independently:
  - Shift: `kbd_lshift`, `kbd_rshift`
  - Ctrl: `kbd_lctrl`, `kbd_rctrl`
  - Alt: `kbd_lalt`, `kbd_ralt`
- Aggregate flags are derived after each modifier transition:
  - `kbd_shift`
  - `kbd_ctrl`
  - `kbd_alt`

## Translation
- Character translation comes from the active `struct keymap`.
- The default keymap is the built-in US layout exposed through the keymap API.
- `Ctrl+A..Z` is translated to `0x01..0x1A`.
- `Ctrl+[` / `Ctrl+\` / `Ctrl+]` produce `ESC`, `FS`, and `GS`.
- With Num Lock cleared, keypad navigation keys emit ANSI cursor/editing sequences.
- The same path produces the shell-relevant bytes for `Ctrl+C`, `Ctrl+D`, and `Ctrl+Z`.
- The current implementation handles:
  - printable single-byte Set 1 make codes
  - single-byte modifier break codes
  - extended right Ctrl / right Alt make and break sequences
  - ANSI escape generation for function keys and navigation keys
  - `Alt+F1..F12` VT switching across the full 12-console range
  - `Ctrl+F9` kernel process dump hook

## Output Paths
- Translated characters are buffered in the 256-byte `kbd_buffer` ring.
- Characters are pushed to the active VT's TTY with `tty_flip_buffer_push()`.
- If the active VT has no attached TTY, the driver falls back to
  `console_push_char()`.
- Input-event notifications are emitted for both key press and key release via
  `input_report_key(..., value)` followed by `input_sync()`.

## Current Limits
- No Set 2 decoding yet.
- No compose/dead-key implementation yet.
- No alternate non-US layouts are registered yet.
