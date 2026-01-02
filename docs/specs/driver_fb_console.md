# Framebuffer Console Specification

## Overview
The framebuffer console provides a text-based interface on top of the graphical framebuffer. It enables kernel logging and user output when VGA text mode is unavailable or when a higher resolution is desired.

## Implementation
- **Font:** Uses an 8x8 bitmap font (built-in).
- **Positioning:** Tracks `cursor_x` and `cursor_y` in pixels.
- **Rendering:** `fb_putc` blits character glyphs to the framebuffer memory.
- **Scrolling:** Performed via software `memcpy` when the cursor reaches the bottom of the screen.
- **Integration:** The `vga_write` function is hooked to automatically use the framebuffer if initialized and active.

## API
### `void fb_putc(char c, uint32_t fg, uint32_t bg)`
Renders a single character at the current cursor position.

### `void fb_write(const char *s, size_t n)`
Writes a string of characters to the framebuffer.

## Constraints
- Font is currently limited to a subset of ASCII.
- Scrolling performance depends on `memcpy` efficiency.
- Supports only 32-bit color depth.
