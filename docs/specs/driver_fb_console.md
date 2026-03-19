# Framebuffer Console Specification

## Overview
The framebuffer console provides a text-based interface on top of the graphical framebuffer. It enables kernel logging and user output when VGA text mode is unavailable or when a higher resolution is desired. All implementation resides in `sys/drivers/video/`.

## Font Subsystem

### Font Parsers
Multiple font formats are supported, parsed at load time into a common `struct font_info`:

| Format | Parser | Notes |
|--------|--------|-------|
| PSF1 | `psf.c` | Legacy 256-glyph format, optional Unicode table |
| PSF2 | `psf.c` | Variable glyph count/size, UTF-8 Unicode table |
| BDF | `bdf_pcf.c` | Text-based X11 bitmap format, two-pass hex parser |
| PCF | `bdf_pcf.c` | Binary compiled X11 format, TOC-navigated sections |

Auto-detection via `psf_parse()` selects PSF1 or PSF2 based on magic bytes.

### Glyph Cache
`font_cache.c` implements a Unicode-aware glyph cache:
- **Hash table:** 256 buckets using FNV-1a hash
- **Unicode mapping:** Parses PSF1/PSF2 Unicode tables to map codepoints → glyph indices
- **UTF-8 decoder:** Full multi-byte UTF-8 decoding for input characters
- **Lookup:** `glyph_cache_get(cache, codepoint)` returns a pointer to the bitmap glyph data

### Built-in Font
A built-in 8x16 PSF font is registered via `font_init_builtin()` at boot.

## Blitting Operations
`fb_ops.c` provides hardware-accelerated-style blitting with 32bpp fast paths:

### `fb_fillrect(fb, info)`
Fills a rectangular region. Supports `ROP_COPY` and `ROP_XOR` raster operations. Includes viewport clipping.

### `fb_copyarea(fb, info)`
Copies a rectangular region with overlap-safe handling (forward/backward copy depending on direction).

### `fb_imageblit(fb, info)`
Blits a 1bpp (monochrome) or color image to the framebuffer. Used for glyph rendering. Monochrome mode maps fg/bg colors per bit. Transparent background supported via `FB_COLOR_TRANSPARENT`.

All operations clip to framebuffer bounds and provide generic fallback paths for non-32bpp modes.

## Character Rendering

### `fb_putc(char c, uint32_t fg, uint32_t bg)`
Renders a single ASCII character at the current cursor position using the active font and `fb_imageblit`.

### `fb_putc_attr(char c, uint32_t fg, uint32_t bg, uint32_t attr)`
Renders a character with text attributes applied to the glyph bitmap before blitting:

| Attribute | Constant | Effect |
|-----------|----------|--------|
| Bold | `FB_ATTR_BOLD` | OR glyph with itself shifted right 1px |
| Italic | `FB_ATTR_ITALIC` | Shear upper half of glyph right by 1px |
| Underline | `FB_ATTR_UNDERLINE` | Fill bottom row of glyph |
| Strikethrough | `FB_ATTR_STRIKETHROUGH` | Fill middle row of glyph |
| Reverse | `FB_ATTR_REVERSE` | Swap foreground/background colors |

Attributes can be combined via bitwise OR.

### `fb_write(const char *s, size_t n)`
Writes a string of characters to the framebuffer console.

## Format Conversion
`fb_get_raw_pixel()` converts canonical RGB colors into the active framebuffer's
native storage format. The current implementation covers:
- direct-color 15/16/24/32bpp layouts using channel-offset/length scaling
- 8bpp indexed output through palette adaptation
- packed 1/2/4bpp fallback packing in `linear_fb_putpixel()`

## Scrolling
Performed via `fb_copyarea` (overlap-safe blit) when the cursor reaches the bottom of the screen. The bottom line is cleared with `fb_fillrect`.

## Integration
The `vga_write` function is hooked to automatically use the framebuffer console if initialized and active. This allows seamless kernel logging through the standard console path.

## Constraints
- Built-in font is limited to the PSF glyph set (typically 256 or 512 glyphs).
- BDF/PCF parsers require the full font data to be memory-resident at parse time.
- 32bpp mode uses optimized fast paths; other depths use a generic bit-level fallback.
