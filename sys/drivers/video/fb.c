#include "fb.h"
#include "../../drivers/video/vga.h"
#include <stddef.h>

static fb_info_t fb;

void fb_init(multiboot_info_t *mbi) {
    if (!(mbi->flags & (1 << 12))) {
        vga_write("FB: Multiboot info has no framebuffer.\n", 39);
        return;
    }

    fb.addr = (uint32_t *)(uintptr_t)mbi->framebuffer_addr;
    fb.width = mbi->framebuffer_width;
    fb.height = mbi->framebuffer_height;
    fb.pitch = mbi->framebuffer_pitch;
    fb.bpp = mbi->framebuffer_bpp;

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
}
