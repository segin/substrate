#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kern/console.h>
#include <arch/x86-common/include/io.h>
#include <drivers/video/cga.h>
#include <drivers/video/fb.h>

void cga_init(void) {
    kprint("CGA: Wrapper initialized (Text mode handled by vga_text).\n");
}

void cga_setup(const char *arg) {
    (void)arg;
}

static int cga_probe(void) {
    /* Similar to Hercules, weak probe for now */
    return 0;
}
static int cga_driver_init(fb_info_t *fb) {
    (void)fb;
    /* CGA Graphics Init Stubs */
    /* If we implemented 320x200x4 mode, we'd set fb info here. */
    kprint("CGA: Graphics mode not fully implemented yet.\n");
    return -1; 
}

static video_driver_t cga_driver = {
    .name = "cga",
    .priority = 4,
    .probe = cga_probe,
    .init = cga_driver_init
};

void cga_install(void) {
    video_register_driver(&cga_driver);
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

