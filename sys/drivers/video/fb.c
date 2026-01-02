#include "fb.h"
#include "font.h"
#include "../../drivers/video/vga.h"
#include <stddef.h>

static fb_info_t fb;
static int cursor_x = 0;
static int cursor_y = 0;
int fb_active = 0;

void fb_init(multiboot_info_t *mbi) {
    if (!(mbi->flags & (1 << 12))) {
        vga_write("FB: Multiboot info has no framebuffer.\n", 39);
        fb_active = 0;
        return;
    }

    fb.addr = (uint32_t *)(uintptr_t)mbi->framebuffer_addr;
    fb.width = mbi->framebuffer_width;
    fb.height = mbi->framebuffer_height;
    fb.pitch = mbi->framebuffer_pitch;
    fb.bpp = mbi->framebuffer_bpp;

    fb_active = 1;
    vga_write("FB: Initialized framebuffer.\n", 30);
}

void fb_putpixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= (int)fb.width || y < 0 || y >= (int)fb.height) return;
    
    // Assuming 32-bit RGB for now
    uint32_t *pixel = (uint32_t *)((uintptr_t)fb.addr + y * fb.pitch + x * (fb.bpp / 8));
    *pixel = color;
}

void fb_clear(uint32_t color) {
    for (uint32_t y = 0; y < fb.height; y++) {
        for (uint32_t x = 0; x < fb.width; x++) {
            fb_putpixel(x, y, color);
        }
    }
    cursor_x = 0;
    cursor_y = 0;
}

void fb_putc(char c, uint32_t fg, uint32_t bg) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y += 8;
    } else if (c == '\r') {
        cursor_x = 0;
    } else {
        if (c >= 32 && c <= 126) {
            const uint8_t *glyph = font_8x8[c - 32];
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    if (glyph[y] & (0x80 >> x)) {
                        fb_putpixel(cursor_x + x, cursor_y + y, fg);
                    } else if (bg != 0xFFFFFFFF) { // Transparent bg if FFFFFFFF
                        fb_putpixel(cursor_x + x, cursor_y + y, bg);
                    }
                }
            }
        }
        cursor_x += 8;
    }

    if (cursor_x >= (int)fb.width) {
        cursor_x = 0;
        cursor_y += 8;
    }

    if (cursor_y + 8 > (int)fb.height) {
        // Scroll up by 8 pixels
        extern void *memcpy(void *dest, const void *src, size_t n);
        void *dst = fb.addr;
        void *src = (void*)((uintptr_t)fb.addr + 8 * fb.pitch);
        size_t size = (fb.height - 8) * fb.pitch;
        memcpy(dst, src, size);
        
        // Clear bottom 8 pixels
        for (uint32_t y = fb.height - 8; y < fb.height; y++) {
            for (uint32_t x = 0; x < fb.width; x++) {
                fb_putpixel(x, y, bg != 0xFFFFFFFF ? bg : 0);
            }
        }
        cursor_y -= 8;
    }
}

void fb_write(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        fb_putc(s[i], 0x00FFFFFF, 0x00000000); // White on Black
    }
}
