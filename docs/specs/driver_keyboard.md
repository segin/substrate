# Keyboard (PS/2) Driver Specification

## Overview
The keyboard driver handles input from standard PS/2 keyboard devices. It decodes scancodes from Set 1 and provides an ASCII interface via a circular buffer.

## Scancode Decoding
- **State Machine:** Tracks extended scancodes (prefixed with `0xE0`).
- **Set 1:** Maps scancodes directly to characters or actions.
- **Modifiers:** Tracks the state of Shift, Ctrl, and Alt keys. Uses a shifted keymap when Shift is active.
- **Keyups:** Handled for modifier keys; ignored for others.

## Input Buffer
- **Type:** Circular buffer.
- **Size:** 256 characters.
- **Interrupt Safety:** The handler pushes characters while the consumer pops them.

## API
### `void keyboard_init(void)`
Initializes the PS/2 controller and the keyboard driver state.

### `void keyboard_handler(registers_t *regs)`
ISR for IRQ1. Reads from port 0x60 and pushes decoded characters to the buffer.

### `char keyboard_getc(void)`
Pops and returns the next character from the input buffer. Returns 0 if empty.

## Constraints
- Only supports US QWERTY layout.
- No support for Shift, Ctrl, Alt modifiers yet.
- Polling `keyboard_getc` is non-blocking.
