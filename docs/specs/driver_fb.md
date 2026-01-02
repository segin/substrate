# Framebuffer Driver Specification

## Overview
The framebuffer driver provides a generic interface for interacting with graphical displays initialized by the bootloader (Multiboot).

## Implementation
- **Data Source:** Parses the Multiboot information structure for framebuffer details (address, width, height, pitch, BPP).
- **Format:** Primarily supports 32-bit RGB/RGBA modes.
- **Memory Mapping:** Uses the physical address provided by Multiboot. (Note: In a full VMM, this memory would be mapped into the kernel address space).

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
