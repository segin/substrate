#ifndef _FB_H
#define _FB_H

#include <stdint.h>
#include <stddef.h>
#include <arch/x86-common/multiboot.h>

typedef struct {
    uint32_t *addr;           /* kernel virtual mapping (from ioremap_wc) */
    uintptr_t phys;           /* physical base — what userspace mmap PTEs point at */
    uint32_t width;
    uint32_t height;
    uint32_t virt_height; /* Virtual height for hardware scrolling */
    uint32_t pitch;
    uint8_t  bpp;
    uint8_t  red_offset;
    uint8_t  red_length;
    uint8_t  green_offset;
    uint8_t  green_length;
    uint8_t  blue_offset;
    uint8_t  blue_length;
    void (*putpixel)(int x, int y, uint32_t color);
    uint32_t virt_width;
    int (*set_viewport)(int x, int y);
    void (*scroll)(int y_offset);
    void (*flush)(int x, int y, int w, int h);
    /* Optional 2-D accelerator hooks.  When set, fb_copyarea() /
     * fb_fillrect() dispatch through these instead of going through
     * fb_putpixel.  Planar drivers (VGA mode 12, etc.) install
     * these to do byte-at-a-time region operations via the VGA
     * write-mode-1 latch path or Set/Reset fill — orders of
     * magnitude faster than per-pixel through the planar putpixel.
     * Arguments are pre-clipped to fb bounds by fb_ops.c. */
    void (*copyarea)(uint32_t dx, uint32_t dy, uint32_t w, uint32_t h,
                     uint32_t sx, uint32_t sy);
    void (*fillrect)(uint32_t dx, uint32_t dy, uint32_t w, uint32_t h,
                     uint32_t color);
    /* Optional indexed-blit accelerator.  When set, the fb_console shadow
     * buffer (8bpp colour indices, 0..15) is pushed to the device through
     * this hook instead of the generic linear conversion.  Planar VGA/EGA
     * drivers install it to do batched plane writes — the index maps
     * directly onto the 4 bit planes, so a whole region costs a handful of
     * port writes instead of ~8 per pixel.  `src` is the index buffer,
     * `src_pitch` its row stride in bytes; the rect is pre-clipped. */
    void (*blit_indexed)(const uint8_t *src, uint32_t src_pitch,
                         uint32_t dx, uint32_t dy, uint32_t w, uint32_t h);
} fb_info_t;

extern fb_info_t fb;
extern int fb_active;

/* Maximum number of simultaneous framebuffer devices */
#define FB_MAX_DEVICES 8

/* Multi-framebuffer registry */
extern fb_info_t fb_devices[FB_MAX_DEVICES];
extern int fb_device_count;

/* Core framebuffer operations */
void fb_init(multiboot_info_t *mbi);
void fb_putpixel(int x, int y, uint32_t color);
void fb_clear(uint32_t color);

/* Register an additional framebuffer device; returns index or -1 */
int fb_register_device(fb_info_t *info);

/* Framebuffer ownership across VT switches.  fb_set_offscreen() redirects
 * the X server's /dev/fb0 mmap to an offscreen shadow (1) or back to the
 * real framebuffer (0); fb_client_clear() drops the registration when the
 * owning process exits.  See fb.c for details. */
void fb_set_offscreen(int offscreen);
void fb_client_clear(void *owner);

/* Console operations are in fb_console.h */
#include "fb_console.h"

#include <sys/fb.h>

/* Video Driver Interface */
typedef struct video_driver {
    const char *name;
    int priority;
    /* Probe: Check if hardware exists. Returns 0 on success. */
    int (*probe)(void);
    /* Init: Initialize hardware and populate fb buffer info. Returns 0 on success. */
    int (*init)(fb_info_t *fb);
    /* Mode Setting */
    int (*list_modes)(struct video_mode_info *modes, int max_count);
    int (*set_mode)(int mode_id);
    /* Viewport Setting (Hardware Scrolling) */
    int (*set_viewport)(int x, int y);
    
    struct video_driver *next;
} video_driver_t;

void video_register_driver(video_driver_t *drv);
int video_ask_mode(fb_info_t *fb);
int video_set_viewport(int x, int y);

/* Parse vga=WxH@BPP or vga=WxH command line and find matching mode */
int fb_parse_vga_mode(const char *arg, uint32_t *width, uint32_t *height, uint32_t *bpp);

/* Linear framebuffer putpixel (exported for driver use) */
void linear_fb_putpixel(int x, int y, uint32_t color);

#endif /* _FB_H */
