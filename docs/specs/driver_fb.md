# Framebuffer Driver Specification

## Overview
The framebuffer driver provides a generic interface for interacting with graphical displays initialized by the bootloader (Multiboot).

## Implementation
- **Data Source:** Parses the Multiboot information structure for framebuffer details (address, width, height, pitch, BPP).
- **EFI GOP Integration:** EFI boots translate GOP framebuffer state into Multiboot framebuffer fields. The generic framebuffer core preserves firmware-provided layouts (RGBX/BGRX). The EFI boot stub enumerates modes and selects the highest 32-bit linear mode ≤ 1920x1200.
- **Format:** Primarily supports 32-bit RGB/RGBA modes.
- **Resolution Selection:** The `vga=WxH@BPP` command line parameter selects a resolution across registered drivers. Legacy VGA graphics modes are also supported via `vga=`.
- **BGA Support:** The BGA driver supports per-mode selection via `vga=`.
- **Multi-Framebuffer Registry:** Supports up to 8 simultaneous devices (`/dev/fb0` through `/dev/fb7`). Additional monitors register via `fb_register_device()`.
- **GRUB Inheritance:** Framebuffers initialized by GRUB are inherited by the multiboot driver.
- **Memory Mapping:** Uses the physical address provided by Multiboot.

## API
### `void fb_init(multiboot_info_t *mbi)`
Initializes the driver from Multiboot information.

### `void fb_putpixel(int x, int y, uint32_t color)`
Plots a pixel at the specified coordinates.

### `void fb_clear(uint32_t color)`
Clears the entire screen with a single color.

## Constraints
- No hardware acceleration.
- Only supports the primary boot-time framebuffer.
- Assumes 32-bit color depth for coordinate calculations.
