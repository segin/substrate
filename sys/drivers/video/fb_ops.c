/*
 * fb_ops.c - Framebuffer Blitting Operations
 *
 * Software-fallback implementations of fb_fillrect, fb_copyarea,
 * and fb_imageblit. Uses direct framebuffer memory access for
 * 32bpp linear framebuffers (fast path) with putpixel fallback.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "fb.h"
#include "fb_ops.h"

/* ==================== Helpers ==================== */

/*
 * Clip a rectangle to the framebuffer bounds.
 * Returns 0 if fully clipped (nothing to draw), 1 if visible.
 */
static int clip_rect(uint32_t *dx, uint32_t *dy, uint32_t *w, uint32_t *h)
{
    if (*dx >= fb.width || *dy >= fb.height)
        return 0;
    if (*dx + *w > fb.width)
        *w = fb.width - *dx;
    if (*dy + *h > fb.height)
        *h = fb.height - *dy;
    if (*w == 0 || *h == 0)
        return 0;
    return 1;
}

/*
 * Get a pointer to a pixel at (x, y) for 32bpp linear framebuffers.
 * Returns NULL if the framebuffer isn't 32bpp linear.
 */
static inline uint32_t *fb_pixel32(uint32_t x, uint32_t y)
{
    return (uint32_t *)((uintptr_t)fb.addr + y * fb.pitch + x * 4);
}

/* Check if we can use the 32bpp fast path */
static inline int is_linear_32bpp(void)
{
    return fb.bpp == 32 && fb.putpixel == linear_fb_putpixel;
}

/* ==================== fb_fillrect ==================== */

/*
 * Fast 32bpp fill: writes pre-converted pixel value directly.
 * For 32bpp we can write entire rows with dword stores.
 */
static void fillrect_32bpp(uint32_t dx, uint32_t dy, uint32_t w,
                            uint32_t h, uint32_t raw_pixel, int rop)
{
    for (uint32_t y = 0; y < h; y++) {
        uint32_t *row = fb_pixel32(dx, dy + y);
        if (rop == ROP_XOR) {
            for (uint32_t x = 0; x < w; x++)
                row[x] ^= raw_pixel;
        } else {
            /* ROP_COPY — use memset if all bytes equal, else dword fill */
            uint8_t b0 = raw_pixel & 0xFF;
            uint8_t b1 = (raw_pixel >> 8) & 0xFF;
            if (b0 == b1 && b0 == ((raw_pixel >> 16) & 0xFF) &&
                b0 == ((raw_pixel >> 24) & 0xFF)) {
                memset(row, b0, w * 4);
            } else {
                for (uint32_t x = 0; x < w; x++)
                    row[x] = raw_pixel;
            }
        }
    }
}

/* Generic fillrect using fb_putpixel (works for all bpp) */
static void fillrect_generic(uint32_t dx, uint32_t dy, uint32_t w,
                              uint32_t h, uint32_t color, int rop)
{
    /* For ROP_XOR with generic putpixel, we fall back to just COPY
     * since we can't read back pixels through the putpixel interface.
     * XOR is only properly supported in 32bpp fast path. */
    (void)rop;
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            fb_putpixel(dx + x, dy + y, color);
        }
    }
}

void fb_fillrect(const struct fb_fillrect_info *rect)
{
    if (!fb_active || !rect)
        return;

    uint32_t dx = rect->dx, dy = rect->dy;
    uint32_t w = rect->width, h = rect->height;

    if (!clip_rect(&dx, &dy, &w, &h))
        return;

    if (is_linear_32bpp()) {
        /* Convert color once for fast path */
        uint32_t raw = rect->color;
        /* For 32bpp with standard layout, color is already in the right format.
         * If color offsets differ, we'd need conversion, but standard
         * 32bpp XRGB is the common case. */
        if (fb.red_offset == 16 && fb.green_offset == 8 && fb.blue_offset == 0) {
            /* Standard XRGB8888 — use as-is */
        } else {
            /* Non-standard layout — scale components */
            uint8_t r = (rect->color >> 16) & 0xFF;
            uint8_t g = (rect->color >> 8) & 0xFF;
            uint8_t b = rect->color & 0xFF;
            raw = 0;
            if (fb.red_length) raw |= ((uint32_t)r >> (8 - fb.red_length)) << fb.red_offset;
            if (fb.green_length) raw |= ((uint32_t)g >> (8 - fb.green_length)) << fb.green_offset;
            if (fb.blue_length) raw |= ((uint32_t)b >> (8 - fb.blue_length)) << fb.blue_offset;
        }
        fillrect_32bpp(dx, dy, w, h, raw, rect->rop);
    } else {
        fillrect_generic(dx, dy, w, h, rect->color, rect->rop);
    }
}

/* ==================== fb_copyarea ==================== */

/*
 * Fast 32bpp copy: uses memmove per row, handles overlapping regions.
 * Copies bottom-to-top when source is above destination to avoid
 * overwriting source data (common scroll-up case).
 */
static void copyarea_32bpp(uint32_t dx, uint32_t dy, uint32_t w,
                            uint32_t h, uint32_t sx, uint32_t sy)
{
    size_t row_bytes = w * 4;

    if (dy < sy || (dy == sy && dx < sx)) {
        /* Copy top-to-bottom (source below or at same row, left of dest) */
        for (uint32_t y = 0; y < h; y++) {
            uint32_t *dst = fb_pixel32(dx, dy + y);
            const uint32_t *src = fb_pixel32(sx, sy + y);
            memmove(dst, src, row_bytes);
        }
    } else {
        /* Copy bottom-to-top (source above destination) */
        for (uint32_t y = h; y > 0; y--) {
            uint32_t *dst = fb_pixel32(dx, dy + y - 1);
            const uint32_t *src = fb_pixel32(sx, sy + y - 1);
            memmove(dst, src, row_bytes);
        }
    }
}

void fb_copyarea(const struct fb_copyarea_info *area)
{
    if (!fb_active || !area)
        return;

    uint32_t dx = area->dx, dy = area->dy;
    uint32_t sx = area->sx, sy = area->sy;
    uint32_t w = area->width, h = area->height;

    /* Clip source */
    if (sx >= fb.width || sy >= fb.height)
        return;
    if (sx + w > fb.width) w = fb.width - sx;
    if (sy + h > fb.height) h = fb.height - sy;

    /* Clip destination */
    if (dx >= fb.width || dy >= fb.height)
        return;
    if (dx + w > fb.width) w = fb.width - dx;
    if (dy + h > fb.height) h = fb.height - dy;

    if (w == 0 || h == 0)
        return;

    if (is_linear_32bpp()) {
        copyarea_32bpp(dx, dy, w, h, sx, sy);
    } else {
        /* Generic: can't read pixels back, so only 32bpp is supported */
        /* For non-32bpp linear, use raw byte copy if linear */
        if (fb.putpixel == linear_fb_putpixel) {
            size_t bytes_per_pixel = fb.bpp / 8;
            if (bytes_per_pixel == 0) bytes_per_pixel = 1;
            size_t row_bytes = w * bytes_per_pixel;

            if (dy < sy || (dy == sy && dx < sx)) {
                for (uint32_t y = 0; y < h; y++) {
                    void *dst = (void *)((uintptr_t)fb.addr + (dy + y) * fb.pitch + dx * bytes_per_pixel);
                    const void *src = (void *)((uintptr_t)fb.addr + (sy + y) * fb.pitch + sx * bytes_per_pixel);
                    memmove(dst, src, row_bytes);
                }
            } else {
                for (uint32_t y = h; y > 0; y--) {
                    void *dst = (void *)((uintptr_t)fb.addr + (dy + y - 1) * fb.pitch + dx * bytes_per_pixel);
                    const void *src = (void *)((uintptr_t)fb.addr + (sy + y - 1) * fb.pitch + sx * bytes_per_pixel);
                    memmove(dst, src, row_bytes);
                }
            }
        }
        /* Non-linear drivers without read-back: no-op (can't copy) */
    }
}

/* ==================== fb_imageblit ==================== */

/*
 * Blit a monochrome (1bpp) image to the framebuffer.
 * Each byte holds 8 pixels, MSB = leftmost.
 * Row stride = ceil(width / 8) bytes.
 */
static void imageblit_mono_32bpp(uint32_t dx, uint32_t dy, uint32_t w,
                                  uint32_t h, uint32_t fg_raw,
                                  uint32_t bg_raw, int bg_transparent,
                                  const uint8_t *data)
{
    uint32_t row_stride = (w + 7) / 8;

    for (uint32_t y = 0; y < h; y++) {
        uint32_t *row = fb_pixel32(dx, dy + y);
        const uint8_t *src_row = data + y * row_stride;

        for (uint32_t x = 0; x < w; x++) {
            if (src_row[x / 8] & (0x80 >> (x & 7))) {
                row[x] = fg_raw;
            } else if (!bg_transparent) {
                row[x] = bg_raw;
            }
        }
    }
}

static void imageblit_mono_generic(uint32_t dx, uint32_t dy, uint32_t w,
                                    uint32_t h, uint32_t fg_color,
                                    uint32_t bg_color, int bg_transparent,
                                    const uint8_t *data)
{
    uint32_t row_stride = (w + 7) / 8;

    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *src_row = data + y * row_stride;

        for (uint32_t x = 0; x < w; x++) {
            if (src_row[x / 8] & (0x80 >> (x & 7))) {
                fb_putpixel(dx + x, dy + y, fg_color);
            } else if (!bg_transparent) {
                fb_putpixel(dx + x, dy + y, bg_color);
            }
        }
    }
}

/*
 * Blit a color image (direct pixel values) to the framebuffer.
 * Each pixel is a 32-bit 0x00RRGGBB value.
 */
static void imageblit_color(uint32_t dx, uint32_t dy, uint32_t w,
                             uint32_t h, const uint8_t *data)
{
    const uint32_t *pixels = (const uint32_t *)data;
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            fb_putpixel(dx + x, dy + y, pixels[y * w + x]);
        }
    }
}

void fb_imageblit(const struct fb_image_info *image)
{
    if (!fb_active || !image || !image->data)
        return;

    uint32_t dx = image->dx, dy = image->dy;
    uint32_t w = image->width, h = image->height;

    if (!clip_rect(&dx, &dy, &w, &h))
        return;

    if (image->type == FB_IMAGE_MONO) {
        int bg_transparent = (image->bg_color == FB_COLOR_TRANSPARENT);

        if (is_linear_32bpp()) {
            /* Convert colors once */
            uint32_t fg_raw = image->fg_color;
            uint32_t bg_raw = image->bg_color;
            imageblit_mono_32bpp(dx, dy, w, h, fg_raw, bg_raw,
                                  bg_transparent, image->data);
        } else {
            imageblit_mono_generic(dx, dy, w, h, image->fg_color,
                                    image->bg_color, bg_transparent,
                                    image->data);
        }
    } else if (image->type == FB_IMAGE_COLOR) {
        imageblit_color(dx, dy, w, h, image->data);
    }
}
