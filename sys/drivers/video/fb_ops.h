/*
 * fb_ops.h - Framebuffer Blitting Operations
 *
 * Accelerated and software-fallback blitting primitives:
 *   fb_fillrect()  - solid color rectangle fill
 *   fb_copyarea()  - screen-to-screen blit
 *   fb_imageblit() - mono/color image to framebuffer
 */
#ifndef _FB_OPS_H
#define _FB_OPS_H

#include <stdint.h>
#include <stddef.h>

/* Raster operations for fb_fillrect */
#define ROP_COPY  0  /* Destination = color */
#define ROP_XOR   1  /* Destination ^= color */

/* Special color value: transparent background (skip pixel) */
#define FB_COLOR_TRANSPARENT 0xFFFFFFFF

struct fb_fillrect_info {
    uint32_t dx;       /* Destination X */
    uint32_t dy;       /* Destination Y */
    uint32_t width;    /* Rectangle width */
    uint32_t height;   /* Rectangle height */
    uint32_t color;    /* Fill color (0x00RRGGBB) */
    int      rop;      /* Raster operation (ROP_COPY or ROP_XOR) */
};

struct fb_copyarea_info {
    uint32_t dx;       /* Destination X */
    uint32_t dy;       /* Destination Y */
    uint32_t width;    /* Area width */
    uint32_t height;   /* Area height */
    uint32_t sx;       /* Source X */
    uint32_t sy;       /* Source Y */
};

/* Image types for fb_imageblit */
#define FB_IMAGE_MONO  0  /* 1bpp: fg/bg colors from info */
#define FB_IMAGE_COLOR 1  /* Direct color pixels */

struct fb_image_info {
    uint32_t dx;       /* Destination X */
    uint32_t dy;       /* Destination Y */
    uint32_t width;    /* Image width in pixels */
    uint32_t height;   /* Image height in pixels */
    uint32_t fg_color; /* Foreground for mono images */
    uint32_t bg_color; /* Background for mono images (TRANSPARENT=skip) */
    int      type;     /* FB_IMAGE_MONO or FB_IMAGE_COLOR */
    const uint8_t *data; /* Image data */
};

/* Fill a rectangle with a solid color */
void fb_fillrect(const struct fb_fillrect_info *rect);

/* Copy a rectangular area within the framebuffer */
void fb_copyarea(const struct fb_copyarea_info *area);

/* Blit a mono or color image to the framebuffer */
void fb_imageblit(const struct fb_image_info *image);

#endif /* _FB_OPS_H */
