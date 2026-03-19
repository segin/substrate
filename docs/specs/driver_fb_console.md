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

## Dirty Tracking
The framebuffer console maintains a single accumulated dirty rectangle over the
current frame. Character draws, clears, and scroll operations expand that
rectangle so later deferred-flush logic can submit one bounded update instead of
rewriting the whole visible surface on every write.

## Deferred Presentation
Dirty framebuffer-console output is presented on a timer-driven cadence rather
than forcing an immediate per-character flush. Drivers that need an explicit
present step may provide an `fb.flush(x, y, w, h)` callback; the console batches
all writes observed during the interval into one rectangle and submits that
region on the next flush tick.

## Mapping Policy
Linear framebuffer memory is mapped through `ioremap_wc()` when the i386 PAT
path is available. The CPU feature probe programs a dedicated PAT slot for write
combining and the PMAP layer preserves `PTE_PWT`/`PTE_PCD`/`PTE_PAT` on page
entries so framebuffer mappings can use WC semantics instead of the older
always-uncached path. Machines without PAT support fall back to uncached
`ioremap()` semantics.

## Software Cursor
The framebuffer console uses a software cursor when no hardware cursor path is
available. The current implementation renders an underline cursor by XORing the
bottom two pixel rows of the active text cell. This keeps the cursor visible on
top of arbitrary foreground and background colors without requiring a separate
backing store for the drawn shape itself. The console still preserves the
underlying framebuffer bytes for those underline rows before drawing and
restores them on hide, so later writes, blink transitions, and viewport updates
do not accumulate XOR damage. Cursor hide/show operations participate in
dirty-rectangle tracking so deferred presentation still flushes the correct
bounds.

Cursor blink runs through `fb_console_tick()` using the kernel timer cadence.
When the active VT requests blinking, the software cursor toggles at roughly
2 Hz and each blink transition is dirtied and flushed through the same deferred
presentation path as ordinary text output.

## Integration
The `vga_write` function is hooked to automatically use the framebuffer console if initialized and active. This allows seamless kernel logging through the standard console path.

## VT / TTY Registration
On framebuffer-only boots, the framebuffer console now allocates the standard
VT tty set (`/dev/tty1` through `/dev/tty12`) if no earlier backend has already
claimed those VTs. This keeps the visible graphical console on the normal tty
namespace instead of requiring a framebuffer-specific userspace path. When VGA
text mode has already installed those tty bindings, the framebuffer console
detects the existing ownership and leaves it intact.

TTY writes no longer bypass terminal semantics. The framebuffer VT driver now
feeds tty output through the shared ANSI parser and updates per-VT cursor,
color, attribute, tab-stop, scroll-region, and alternate-screen state before
redrawing the affected framebuffer cells. That makes `/dev/tty[1-N]` on a
framebuffer-only boot behave like a terminal instead of a raw glyph sink.

The framebuffer VT tty backend currently exposes the per-VT controls that are
actually implemented by the graphical console:
- tab width get/set (`VTIOCGTABW`, `VTIOCSTABW`)
- cursor visibility get/set (`VTIOCGCURSOR`, `VTIOCSCURSOR`)
- cursor blink get/set (`VTIOCGCURBLINK`, `VTIOCSCURBLINK`)

VGA text-blink mode ioctls remain specific to the hardware text backend and are
rejected by the framebuffer tty path.

Framebuffer VT tty registration also refreshes `winsize` from the active text
geometry (`ws_col = cols`, `ws_row = visible_rows`) for both newly created tty
instances and preexisting VT tty ownership. That keeps generic
`TIOCGWINSZ`/`TIOCSWINSZ` behavior aligned with the framebuffer console layout.

## Constraints
- Built-in font is limited to the PSF glyph set (typically 256 or 512 glyphs).
- BDF/PCF parsers require the full font data to be memory-resident at parse time.
- 32bpp mode uses optimized fast paths; other depths use a generic bit-level fallback.
