#ifndef _FB_H
#define _FB_H

#include <stdint.h>
#include <stddef.h>
#include <arch/x86-common/include/multiboot.h>

typedef struct {
    uint32_t *addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t  bpp;
    void (*putpixel)(int x, int y, uint32_t color);
} fb_info_t;

/* Core framebuffer operations */
void fb_init(multiboot_info_t *mbi);
void fb_putpixel(int x, int y, uint32_t color);
void fb_clear(uint32_t color);

/* Console operations are in fb_console.h */
#include "fb_console.h"

/* Video Driver Interface */
typedef struct video_driver {
    const char *name;
    int priority;
    /* Probe: Check if hardware exists. Returns 0 on success. */
    int (*probe)(void);
    /* Init: Initialize hardware and populate fb buffer info. Returns 0 on success. */
    int (*init)(fb_info_t *fb);
    struct video_driver *next;
} video_driver_t;

void video_register_driver(video_driver_t *drv);

#endif /* _FB_H */
