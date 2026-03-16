#ifndef _FB_H
#define _FB_H

#include <stdint.h>
#include <stddef.h>
#include <arch/x86-common/multiboot.h>

typedef struct {
    uint32_t *addr;
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
