#ifndef _FB_H
#define _FB_H

#include <stdint.h>
#include <stddef.h>
#include "../../arch/x86-common/include/multiboot.h"

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
void fb_putc(char c, uint32_t fg, uint32_t bg);
void fb_write(const char *s, size_t n);

#endif
