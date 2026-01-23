#include <drivers/video/cga.h>
#include <arch/x86-common/include/io.h>
#include <stdint.h>
#include <stddef.h>

#include <string.h> /* For memmove/memset */
#include <kern/console.h>

void cga_init(void) {
    kprint("CGA: Wrapper initialized (Text mode handled by vga_text).\n");
}

void cga_setup(const char *arg) {
    (void)arg;
    // CGA setup is now mostly implicit or handled by vga_text for text mode
    // If we wanted to switch to CGA graphics mode (320x200x4), we would do it here.
}

// Graphics primitives for CGA High Res / Medium Res would go here
void cga_putpixel(int x, int y, uint8_t color) {
    if (x < 0 || x >= 320 || y < 0 || y >= 200) return;
    
    // CGA Memory is interleaved
    // 0xB8000: Even rows (0, 2, 4...)
    // 0xBA000: Odd rows (1, 3, 5...)
    
    int bank = (y % 2) == 0 ? 0 : 1;
    int offset = (y / 2) * 80 + (x / 4);
    
    uint8_t *mem = (uint8_t *)(0xC00B8000 + (bank * 0x2000) + offset);
    
    // 2 bits per pixel (Medium Res)
    int shift = 6 - ((x % 4) * 2);
    uint8_t mask = 0x03 << shift;
    
    *mem = (*mem & ~mask) | ((color & 0x03) << shift);
}

