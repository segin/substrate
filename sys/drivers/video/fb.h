#ifndef _FB_H
#define _FB_H

#include <stdint.h>
#include "../../arch/i386/multiboot.h"

typedef struct {
    uint32_t *addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t  bpp;
} fb_info_t;

void fb_init(multiboot_info_t *mbi);
void fb_putpixel(int x, int y, uint32_t color);
void fb_clear(uint32_t color);

#endif
