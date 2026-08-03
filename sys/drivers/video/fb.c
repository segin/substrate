/*
 * fb.c - Framebuffer Core Driver
 *
 * Initializes framebuffer from Multiboot, handles pixel operations,
 * and registers /dev/fb0 device node.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <arch/i386/pmap.h>
#include <arch/i386/pmm.h>
#include <drivers/video/bga.h>
#include <drivers/video/fb.h>
#include <drivers/video/fb_console.h>
#include <drivers/video/font.h>
#include <drivers/video/hw_text.h>
#include <kern/cmdline.h>
#include <kern/console.h>
#include <kern/resource.h>
#include <sys/copy.h>
#include <sys/errno.h>
#include <sys/fb.h>
#include <sys/file.h>
#include <sys/lock.h>
#include <sys/mman.h>
#include <sys/proc.h>
#include <vfs/vfs.h>
#include <vm/vm_kmem.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_pager.h>

/* ==================== Global State ==================== */

fb_info_t fb;
int fb_active = 0;


/* Multi-framebuffer device registry */
fb_info_t fb_devices[FB_MAX_DEVICES];
int fb_device_count = 0;

/* ==================== Pixel Operations ==================== */

/* Simple RGB to CGA/EGA/VGA Palette Index Mapping */
static uint8_t rgb_to_index(uint32_t color) {
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    
    /* Threshold for "lit" pixel */
    uint8_t i = 0;
    if (r > 64) i |= 4;
    if (g > 64) i |= 2;
    if (b > 64) i |= 1;
    if (r > 128 || g > 128 || b > 128) i |= 8; // Intensity
    
    return i;
}

static uint32_t fb_scale_component(uint8_t component, uint8_t bits) {
    uint64_t max_value;

    if (bits == 0) {
        return 0;
    }
    if (bits >= 31) {
        return (uint32_t)component;
    }
    max_value = ((uint64_t)1 << bits) - 1;
    return (uint32_t)(((uint64_t)component * max_value + 127) / 255);
}

static void fb_set_default_color_layout(fb_info_t *info) {
    if (info->red_length != 0 || info->green_length != 0 || info->blue_length != 0) {
        return;
    }

    switch (info->bpp) {
    case 32:
    case 24:
        info->red_offset = 16;
        info->red_length = 8;
        info->green_offset = 8;
        info->green_length = 8;
        info->blue_offset = 0;
        info->blue_length = 8;
        break;
    case 16:
        info->red_offset = 11;
        info->red_length = 5;
        info->green_offset = 5;
        info->green_length = 6;
        info->blue_offset = 0;
        info->blue_length = 5;
        break;
    case 15:
        info->red_offset = 10;
        info->red_length = 5;
        info->green_offset = 5;
        info->green_length = 5;
        info->blue_offset = 0;
        info->blue_length = 5;
        break;
    default:
        break;
    }
}

static uint32_t fb_get_raw_pixel(uint32_t color, uint8_t bpp) {
    if (bpp >= 15) {
        uint8_t red = (color >> 16) & 0xFF;
        uint8_t green = (color >> 8) & 0xFF;
        uint8_t blue = color & 0xFF;

        fb_set_default_color_layout(&fb);
        return (fb_scale_component(red, fb.red_length) << fb.red_offset) |
            (fb_scale_component(green, fb.green_length) << fb.green_offset) |
            (fb_scale_component(blue, fb.blue_length) << fb.blue_offset);
    } else if (bpp == 8) {
        return rgb_to_index(color);
    }
    return 0;
}

void linear_fb_putpixel(int x, int y, uint32_t color) {
    /* Use virtual dimensions if set, otherwise physical */
    int max_x = fb.virt_width ? (int)fb.virt_width : (int)fb.width;
    int max_y = fb.virt_height ? (int)fb.virt_height : (int)fb.height;

    if (x < 0 || x >= max_x || y < 0 || y >= max_y) return;

    if (fb.bpp >= 15) {
        uint8_t *pixel = (uint8_t *)((uintptr_t)fb.addr + y * fb.pitch + x * (fb.bpp / 8));
        uint32_t val = fb_get_raw_pixel(color, fb.bpp);
        switch (fb.bpp) {
            case 32:
                *(uint32_t *)pixel = val;
                break;
            case 24:
                pixel[0] = val & 0xFF;
                pixel[1] = (val >> 8) & 0xFF;
                pixel[2] = (val >> 16) & 0xFF;
                break;
            case 16:
            case 15:
                *(uint16_t *)pixel = (uint16_t)val;
                break;
        }
    } else {
        /* Packed pixel formats (1, 2, 4, 8 bpp) with Palette Adaptation */
        uint8_t index = rgb_to_index(color);
        
        if (fb.bpp == 8) {
             uint8_t *pixel = (uint8_t *)((uintptr_t)fb.addr + y * fb.pitch + x);
             *pixel = (uint8_t)fb_get_raw_pixel(color, 8);
             return;
        }
        
        uint8_t *pixel;
        uint8_t shift, mask;
        
        switch (fb.bpp) {
            case 4:
                pixel = (uint8_t *)((uintptr_t)fb.addr + y * fb.pitch + x / 2);
                shift = (1 - (x % 2)) * 4;
                mask = 0xF << shift;
                *pixel = (*pixel & ~mask) | ((index & 0xF) << shift);
                break;
            case 2:
                pixel = (uint8_t *)((uintptr_t)fb.addr + y * fb.pitch + x / 4);
                shift = (3 - (x % 4)) * 2;
                mask = 0x3 << shift;
                
                /* Calculate Luma (Y = 0.299R + 0.587G + 0.114B) */
                uint8_t r = (color >> 16) & 0xFF;
                uint8_t g = (color >> 8) & 0xFF;
                uint8_t b = color & 0xFF;
                // Fast approx: (R*77 + G*150 + B*29) >> 8
                uint8_t luma = (r * 77 + g * 150 + b * 29) >> 8;
                
                // Map 0..255 -> 0..3
                uint8_t val = luma >> 6;
                
                *pixel = (*pixel & ~mask) | ((val & 0x3) << shift);
                break;
            case 1:
                pixel = (uint8_t *)((uintptr_t)fb.addr + y * fb.pitch + x / 8);
                shift = 7 - (x % 8);
                mask = 1 << shift;
                
                /* Use Luma for thresholding */
                uint8_t r1 = (color >> 16) & 0xFF;
                uint8_t g1 = (color >> 8) & 0xFF;
                uint8_t b1 = color & 0xFF;
                uint8_t luma1 = (r1 * 77 + g1 * 150 + b1 * 29) >> 8;
                
                if (luma1 > 127) *pixel |= mask;
                else *pixel &= ~mask;
                break;
            default:
                break;
        }
    }
}

void fb_putpixel(int x, int y, uint32_t color) {
    if (fb.putpixel) {
        fb.putpixel(x, y, color);
    }
}

void fb_clear(uint32_t color) {
    /* If the driver is not using the standard linear layout, use the slow path */
    if (fb.putpixel != linear_fb_putpixel) {
        for (uint32_t y = 0; y < fb.height; y++) {
            for (uint32_t x = 0; x < fb.width; x++) {
                fb_putpixel(x, y, color);
            }
        }
        return;
    }

    uintptr_t addr = (uintptr_t)fb.addr;
    uint32_t pitch = fb.pitch;
    uint32_t height = fb.height;
    uint32_t width = fb.width;
    uint8_t bpp = fb.bpp;

    /* Pre-calculate pixel value */
    uint32_t pixel_val = 0;

    if (bpp >= 8) {
        pixel_val = fb_get_raw_pixel(color, bpp);
    } else {
        /* Packed pixel formats */
        uint8_t index = rgb_to_index(color);

        if (bpp == 4) {
            pixel_val = (index & 0xF) | ((index & 0xF) << 4);
        } else if (bpp == 2) {
             /* Luma mapping for 2bpp logic from putpixel is:
                luma = (r*77 + g*150 + b*29) >> 8; val = luma >> 6;
             */
             uint8_t r = (color >> 16) & 0xFF;
             uint8_t g = (color >> 8) & 0xFF;
             uint8_t b = color & 0xFF;
             uint8_t luma = (r * 77 + g * 150 + b * 29) >> 8;
             uint8_t val = (luma >> 6) & 0x3;
             pixel_val = val | (val << 2) | (val << 4) | (val << 6);
        } else if (bpp == 1) {
             uint8_t r = (color >> 16) & 0xFF;
             uint8_t g = (color >> 8) & 0xFF;
             uint8_t b = color & 0xFF;
             uint8_t luma = (r * 77 + g * 150 + b * 29) >> 8;
             uint8_t val = (luma > 127) ? 1 : 0;
             pixel_val = val ? 0xFF : 0x00;
        }
    }

    /* Optimization: If bytes are identical, use memset */
    int use_memset = 0;
    if (bpp == 32) {
        uint8_t b0 = pixel_val & 0xFF;
        uint8_t b1 = (pixel_val >> 8) & 0xFF;
        uint8_t b2 = (pixel_val >> 16) & 0xFF;
        uint8_t b3 = (pixel_val >> 24) & 0xFF;
        if (b0 == b1 && b1 == b2 && b2 == b3) {
            use_memset = 1;
            pixel_val = b0; // For memset argument
        }
    } else if (bpp == 16 || bpp == 15) {
        uint8_t b0 = pixel_val & 0xFF;
        uint8_t b1 = (pixel_val >> 8) & 0xFF;
        if (b0 == b1) {
            use_memset = 1;
            pixel_val = b0;
        }
    } else if (bpp <= 8) {
        use_memset = 1;
    }

    if (use_memset) {
        /* Calculate bytes per line */
        /* For <8bpp, rounding up to byte */
        uint32_t line_bytes = (width * bpp + 7) / 8;

        /* If pitch == line_bytes, we can memset the whole block */
        if (pitch == line_bytes) {
            memset((void*)addr, (uint8_t)pixel_val, pitch * height);
        } else {
            for (uint32_t y = 0; y < height; y++) {
                memset((void*)(addr + y * pitch), (uint8_t)pixel_val, line_bytes);
            }
        }
    } else {
        /* Multi-byte fill loop */
        for (uint32_t y = 0; y < height; y++) {
            uint8_t *row = (uint8_t *)(addr + y * pitch);
            if (bpp == 32) {
                uint32_t *p = (uint32_t *)row;
                for (uint32_t x = 0; x < width; x++) {
                    p[x] = pixel_val;
                }
            } else if (bpp == 24) {
                 uint8_t r = pixel_val & 0xFF;
                 uint8_t g = (pixel_val >> 8) & 0xFF;
                 uint8_t b = (pixel_val >> 16) & 0xFF;
                 for (uint32_t x = 0; x < width; x++) {
                     row[x*3] = r;
                     row[x*3+1] = g;
                     row[x*3+2] = b;
                 }
            } else if (bpp == 16 || bpp == 15) {
                uint16_t *p = (uint16_t *)row;
                uint16_t val16 = (uint16_t)pixel_val;
                for (uint32_t x = 0; x < width; x++) {
                    p[x] = val16;
                }
            }
        }
    }
}

/* ==================== Device Node Operations ==================== */

/* Track the currently active driver */
static video_driver_t *current_driver = NULL;
/* Video driver registry (used by mode matching and init) */
static video_driver_t *video_drivers = NULL;

/* ==================== VGA Mode Parsing ==================== */

/*
 * fb_parse_vga_mode - Parse "WxH@BPP" or "WxH" format string.
 *
 * Examples: "1024x768@32", "640x480", "320x200@8"
 * Returns 0 on success, -1 on parse error.
 */
/* Sane upper bounds — any real display mode is well under these.
 * Keeping the cap small means a malicious cmdline can't drive
 * width*pitch arithmetic into wraparound territory. */
#define FB_MODE_MAX_WIDTH   16384
#define FB_MODE_MAX_HEIGHT  16384
#define FB_MODE_MAX_BPP     64

int fb_parse_vga_mode(const char *arg, uint32_t *width, uint32_t *height, uint32_t *bpp) {
    uint32_t w = 0, h = 0, b = 0;
    const char *p = arg;

    if (!arg || !width || !height || !bpp) return -1;

    /* Parse width */
    while (*p >= '0' && *p <= '9') {
        w = w * 10 + (*p - '0');
        if (w > FB_MODE_MAX_WIDTH) return -1;
        p++;
    }
    if (w == 0 || (*p != 'x' && *p != 'X')) return -1;
    p++; /* skip 'x' */

    /* Parse height */
    while (*p >= '0' && *p <= '9') {
        h = h * 10 + (*p - '0');
        if (h > FB_MODE_MAX_HEIGHT) return -1;
        p++;
    }
    if (h == 0) return -1;

    /* Parse optional @BPP */
    if (*p == '@') {
        p++;
        while (*p >= '0' && *p <= '9') {
            b = b * 10 + (*p - '0');
            if (b > FB_MODE_MAX_BPP) return -1;
            p++;
        }
        if (b == 0) return -1;
    }

    /* Trailing garbage check */
    if (*p != '\0') return -1;

    *width = w;
    *height = h;
    *bpp = b; /* 0 means "any bpp" */
    return 0;
}

/*
 * fb_find_matching_mode - Search all registered drivers for a mode matching
 * the requested WxH@BPP. Returns the best driver and mode_id, preferring
 * higher BPP when bpp==0.
 */
static int fb_find_matching_mode(uint32_t req_w, uint32_t req_h, uint32_t req_bpp,
                                  video_driver_t **out_drv, int *out_mode_id) {
    struct video_mode_info modes[32];
    video_driver_t *best_drv = NULL;
    int best_mode_id = -1;
    int best_bpp = -1;
    int best_prio = -1;

    video_driver_t *drv = video_drivers;
    while (drv) {
        if (drv->probe && drv->probe() != 0) {
            drv = drv->next;
            continue;
        }
        if (!drv->list_modes) {
            drv = drv->next;
            continue;
        }

        int total = drv->list_modes(NULL, 0);
        if (total <= 0) {
            drv = drv->next;
            continue;
        }
        if (total > 32) total = 32;
        int count = drv->list_modes(modes, total);

        for (int i = 0; i < count; i++) {
            if (modes[i].width != req_w || modes[i].height != req_h)
                continue;
            if (req_bpp != 0 && modes[i].bpp != req_bpp)
                continue;

            /* Pick best match: prefer exact BPP, then highest BPP, then highest driver priority */
            int score_bpp = (int)modes[i].bpp;
            int score_prio = drv->priority;

            if (best_drv == NULL ||
                (req_bpp != 0 && score_bpp == (int)req_bpp) ||
                score_bpp > best_bpp ||
                (score_bpp == best_bpp && score_prio > best_prio)) {
                best_drv = drv;
                best_mode_id = modes[i].mode_id;
                best_bpp = score_bpp;
                best_prio = score_prio;
            }
        }
        drv = drv->next;
    }

    if (best_drv) {
        *out_drv = best_drv;
        *out_mode_id = best_mode_id;
        return 0;
    }
    return -1;
}

/* ==================== Multi-Framebuffer Registration ==================== */

int fb_register_device(fb_info_t *info) {
    if (!info || fb_device_count >= FB_MAX_DEVICES) return -1;
    fb_devices[fb_device_count] = *info;
    return fb_device_count++;
}

int video_set_viewport(int x, int y) {
    if (current_driver && current_driver->set_viewport) {
        return current_driver->set_viewport(x, y);
    }
    return -1;
}

static int fb_fs_ioctl(fs_node_t *node, uint32_t request, void *arg) {
    (void)node;
    if (request == FBIOGET_VSCREENINFO) {
        struct fb_var_screeninfo vi;
        if (!arg) return -EINVAL;

        memset(&vi, 0, sizeof(struct fb_var_screeninfo));
        vi.xres = fb.width;
        vi.yres = fb.height;
        vi.xres_virtual = fb.virt_width ? fb.virt_width : fb.width;
        vi.yres_virtual = fb.virt_height ? fb.virt_height : fb.height;
        /* Planar VGA/EGA is exposed to userland as an 8bpp linear shim (see
         * the /dev/fb0 mmap path); report that, not the 4bpp planar depth. */
        vi.bits_per_pixel = (fb.blit_indexed != NULL) ? 8 : fb.bpp;

        fb_set_default_color_layout(&fb);
        if (fb.blit_indexed == NULL &&
            (fb.red_length || fb.green_length || fb.blue_length)) {
            vi.red.offset = fb.red_offset;
            vi.red.length = fb.red_length;
            vi.green.offset = fb.green_offset;
            vi.green.length = fb.green_length;
            vi.blue.offset = fb.blue_offset;
            vi.blue.length = fb.blue_length;
        }

        if (copyout(&vi, arg, sizeof(struct fb_var_screeninfo)) != 0) {
            return -EFAULT;
        }
        return 0;
    } else if (request == FBIOPUT_VSCREENINFO) {
        /* Userland (xorg-server fbdev) writes back a possibly-modified
         * mode after FBIOGET_VSCREENINFO.  Substrate's framebuffer is
         * fixed-at-boot; accept the call and ignore the changes.  X
         * checks the returned struct's xres/yres/bits_per_pixel and
         * proceeds if they match what it asked for — by leaving the
         * mode unchanged but returning success, kdrive treats this
         * as "your requested mode equals my current mode." */
        struct fb_var_screeninfo vi;
        if (!arg) return -EINVAL;
        if (copyin(arg, &vi, sizeof(vi)) != 0) return -EFAULT;
        (void)vi;
        return 0;
    } else if (request == FBIOGET_FSCREENINFO) {
        /* Linux fb_fix_screeninfo — what's static about the device.
         * Ported software (xorg-server's kdrive/fbdev backend) hits
         * this immediately after FBIOGET_VSCREENINFO. */
        struct fb_fix_screeninfo fi;
        if (!arg) return -EINVAL;

        memset(&fi, 0, sizeof(fi));
        strlcpy(fi.id, "substratefb", sizeof(fi.id));
        /* smem_start is the PHYSICAL framebuffer base.  We hand back
         * the kernel virtual address (the consumer mmaps /dev/fb0
         * to get a userland mapping; the absolute physical value
         * isn't meaningful to userland but Linux puts it here for
         * informational use). */
        if (fb.blit_indexed != NULL) {
            /* Planar: report the 8bpp linear shim, not the planar aperture. */
            fi.smem_start = 0;   /* RAM shim, allocated on first mmap */
            fi.smem_len = (unsigned long)((size_t)fb.width * (size_t)fb.height);
            fi.line_length = fb.width;
        } else {
            fi.smem_start = (unsigned long)(uintptr_t)fb.addr;
            fi.smem_len = fb.pitch * fb.height;
            fi.line_length = fb.pitch;
        }
        /* PACKED_PIXELS for both (the planar shim is linear to userland);
         * TRUECOLOR for RGB linear, PSEUDOCOLOR for indexed (incl. planar). */
        fi.type = 0;       /* FB_TYPE_PACKED_PIXELS */
        fi.type_aux = 0;
        fi.visual = (fb.bpp >= 16) ? 2 /* TRUECOLOR */ : 3 /* PSEUDOCOLOR */;
        fi.xpanstep = fb.virt_width > fb.width ? 1 : 0;
        fi.ypanstep = fb.virt_height > fb.height ? 1 : 0;
        fi.ywrapstep = 0;
        fi.mmio_start = 0;
        fi.mmio_len = 0;
        fi.accel = 0;

        if (copyout(&fi, arg, sizeof(fi)) != 0) {
            return -EFAULT;
        }
        return 0;
    } else if (request == FBIOGET_VIDEO_MODES) {
        struct video_mode_query query;
        if (!arg) return -EINVAL;
        if (copyin(arg, &query, sizeof(struct video_mode_query)) != 0) {
            return -EFAULT;
        }

        if (!current_driver || !current_driver->list_modes) return -ENOTTY;
        
        int total = current_driver->list_modes(NULL, 0);

        if (query.modes && query.count > 0) {
            uint32_t to_copy = query.count;
            if (to_copy > (uint32_t)total) to_copy = (uint32_t)total;
            
            struct video_mode_info *kmodes = kmalloc(to_copy * sizeof(struct video_mode_info));
            if (!kmodes) return -ENOMEM;

            current_driver->list_modes(kmodes, (int)to_copy);
            if (copyout(kmodes, query.modes, to_copy * sizeof(struct video_mode_info)) != 0) {
                kfree(kmodes, to_copy * sizeof(struct video_mode_info));
                return -EFAULT;
            }
            kfree(kmodes, to_copy * sizeof(struct video_mode_info));
        }

        query.count = (uint32_t)total;
        if (copyout(&query, arg, sizeof(struct video_mode_query)) != 0) {
            return -EFAULT;
        }
        return 0;
    } else if (request == FBIOPUT_VIDEO_MODE) {
        if (!current_driver || !current_driver->set_mode) return -ENOTTY;
        if (!arg) return -EINVAL;

        uint32_t mode_id;
        if (copyin(arg, &mode_id, sizeof(uint32_t)) != 0) {
            return -EFAULT;
        }
        
        if (current_driver->set_mode(mode_id) == 0) {
            /* set_mode updated the global fb geometry.  Resync the derived
             * state so nothing keeps the old dimensions: the fb0 registry
             * entry and — critically — the console's RAM shadow, which would
             * otherwise be read/written out of bounds when a draw clips to the
             * new (possibly larger) fb.width/height. */
            fb_set_default_color_layout(&fb);
            if (fb.virt_height == 0) fb.virt_height = fb.height;
            if (fb.virt_width == 0) fb.virt_width = fb.width;
            fb_devices[0] = fb;
            fb_console_resize();
            return 0;
        }
        return -EINVAL;
    }
    return -ENOTTY;
}

/* Byte-stream read/write — lets `cat /dev/fb0 > snap` and
 * `cat file.raw > /dev/fb0` work for pixel scribbling.  Most consumers
 * will use mmap(), but the file-IO path is what shells expect.  The
 * `buf` parameter is a kernel pointer (sys_read/sys_write double-buffer
 * via kbuf before reaching the node callback) so a plain memcpy is
 * correct — copyin/copyout would reject it as "not a user address".
 * Bounds-clipped to the framebuffer size; returning 0 at EOF lets the
 * caller see end-of-buffer instead of EIO. */
static size_t fb_fs_read(fs_node_t *node, off_t off, size_t len, uint8_t *buf) {
    (void)node;
    if (!fb_active || !fb.addr) return 0;
    uint64_t fb_size = (uint64_t)fb.pitch * (uint64_t)fb.height;
    if (off < 0 || (uint64_t)off >= fb_size) return 0;
    if ((uint64_t)off + len > fb_size) len = (size_t)(fb_size - off);
    memcpy(buf, (uint8_t *)fb.addr + off, len);
    return len;
}

static size_t fb_fs_write(fs_node_t *node, off_t off, size_t len, const uint8_t *buf) {
    (void)node;
    if (!fb_active || !fb.addr) return 0;
    uint64_t fb_size = (uint64_t)fb.pitch * (uint64_t)fb.height;
    if (off < 0 || (uint64_t)off >= fb_size) return 0;
    if ((uint64_t)off + len > fb_size) len = (size_t)(fb_size - off);
    memcpy((uint8_t *)fb.addr + off, buf, len);
    return len;
}

/* ============ Framebuffer ownership: offscreen shadow buffer ============
 *
 * When the X server's VT is switched out of the foreground its /dev/fb0
 * mmap must stop reaching visible video memory, or its drawing bleeds over
 * the text console that now owns the screen.  We cannot unmap a running X
 * server, so instead we redirect its mapping at a physically-contiguous
 * offscreen shadow buffer: X keeps drawing (now into the shadow), and on
 * switch-back we blit the shadow to the real framebuffer to present its
 * last frame and redirect the mapping back to live video memory.
 *
 * Bounded to framebuffers that fit a single contiguous allocation
 * (PMM_MAX_ORDER => 4 MiB, i.e. up to 1024x768x32).  Larger modes degrade
 * gracefully to the previous behaviour: the redirect becomes a no-op and a
 * backgrounded X keeps drawing to the real framebuffer.
 */
static spinlock_t fb_shadow_lock = SPINLOCK_INIT("fb_shadow");
static void      *fb_shadow_virt;   /* kernel VA of the shadow buffer  */
static uintptr_t  fb_shadow_phys;   /* physical base of the shadow     */
static size_t     fb_shadow_size;   /* bytes (page-rounded)            */
static int        fb_is_offscreen;  /* current redirect state          */

static struct {
    int          active;
    pmap_t       pmap;       /* owning process address space            */
    uintptr_t    va;         /* mmap base within that address space     */
    size_t       len;        /* mapped length (page-rounded)            */
    vm_pager_t  *pager;      /* device pager backing the mapping        */
    uintptr_t    fb_offset;  /* byte offset into the framebuffer        */
    void        *owner;      /* owning process (== vt graphics_owner)   */
} fb_client;

static int fb_shadow_ensure(void) {
    size_t fb_size;
    size_t npages;
    void *v;

    if (fb_shadow_virt) {
        return 1;
    }
    if (!fb.phys || fb.pitch == 0 || fb.height == 0) {
        return 0;
    }
    fb_size = (size_t)fb.pitch * (size_t)fb.height;
    npages = (fb_size + 0xFFFU) >> 12;
    v = pmm_alloc_contiguous(npages);
    if (!v) {
        kprintf("fb: offscreen shadow unavailable — a %u-page framebuffer "
                "exceeds the contiguous allocation limit; a backgrounded X "
                "server will keep drawing to the visible screen\n",
                (unsigned)npages);
        return 0;
    }
    memset(v, 0, npages << 12);
    fb_shadow_virt = v;
    fb_shadow_phys = (uintptr_t)V2P(v);
    fb_shadow_size = npages << 12;
    return 1;
}

/* Record a /dev/fb0 mapping as the framebuffer client (the X server).
 * There is one visible framebuffer, hence one client of interest. */
static void fb_client_register(pmap_t pmap, uintptr_t va, size_t len,
                               vm_pager_t *pager, uintptr_t fb_offset,
                               void *owner) {
    spinlock_acquire(&fb_shadow_lock);
    fb_client.active = 1;
    fb_client.pmap = pmap;
    fb_client.va = va;
    fb_client.len = len;
    fb_client.pager = pager;
    fb_client.fb_offset = fb_offset;
    fb_client.owner = owner;
    fb_is_offscreen = 0;     /* a fresh mapping always points at real video */
    spinlock_release(&fb_shadow_lock);
    /* The shadow is allocated lazily, only the first time X is actually
     * backgrounded (see fb_set_offscreen).  Allocating it here would burn a
     * 4 MiB contiguous block at X startup — exactly when an X session is
     * scrambling for memory — even on a system that never switches VTs. */
}

/*
 * Redirect the framebuffer client between the real framebuffer (onscreen)
 * and the offscreen shadow.  Called from the VT-switch path: offscreen=1
 * when leaving the graphics VT, offscreen=0 when returning to it.
 */
void fb_set_offscreen(int offscreen) {
    pmap_t pmap;
    uintptr_t va, target;
    size_t len;
    vm_pager_t *pager;
    int present;

    offscreen = offscreen ? 1 : 0;

    /* Allocate the shadow lazily, the first time X is actually backgrounded
     * — done outside the lock since the contiguous allocation can scan the
     * buddy free lists.  If it fails (no client, fragmentation, mode larger
     * than one contiguous block) we leave X mapped to the real framebuffer
     * rather than hide it: graceful degradation, never a crash. */
    if (offscreen && !fb_shadow_virt && !fb_shadow_ensure()) {
        return;
    }

    spinlock_acquire(&fb_shadow_lock);
    if (!fb_client.active || !fb_client.pager) {
        spinlock_release(&fb_shadow_lock);
        return;
    }
    if (offscreen == fb_is_offscreen) {
        spinlock_release(&fb_shadow_lock);
        return;
    }
    if (offscreen && !fb_shadow_virt) {
        /* Shadow vanished under us (shouldn't happen): stay onscreen. */
        spinlock_release(&fb_shadow_lock);
        return;
    }
    pmap   = fb_client.pmap;
    va     = fb_client.va;
    len    = fb_client.len;
    pager  = fb_client.pager;
    target = (offscreen ? fb_shadow_phys : fb.phys) + fb_client.fb_offset;
    present = !offscreen;

    /* Going offscreen: seed the shadow with the frame that is on screen
     * right now, before the redirect takes effect.  From here on the
     * shadow IS this VT's screen, so it has to start out holding what the
     * VT was displaying.  A client only redraws in response to damage, and
     * nothing damages an invisible screen, so without this the client
     * typically draws nothing at all while backgrounded and the switch
     * back presents a shadow that has never held a frame — a black
     * screen instead of the session the user left. */
    if (offscreen && fb_shadow_virt && fb.addr) {
        size_t n = fb_shadow_size;
        size_t fb_size = (size_t)fb.pitch * (size_t)fb.height;
        if (n > fb_size) n = fb_size;
        memcpy(fb_shadow_virt, (const void *)fb.addr, n);
    }

    vm_pager_device_set_phys_base(pager, target);
    fb_is_offscreen = offscreen;
    spinlock_release(&fb_shadow_lock);

    /* Coming back onscreen: present the frame X accumulated offscreen. */
    if (present && fb_shadow_virt && fb.addr) {
        size_t n = fb_shadow_size;
        size_t fb_size = (size_t)fb.pitch * (size_t)fb.height;
        if (n > fb_size) n = fb_size;
        memcpy((void *)fb.addr, fb_shadow_virt, n);
    }

    /* Drop the now-stale PTEs; the page-fault handler reinstalls them at
     * the new physical base on the client's next access.  During a VT
     * switch the client is not the running process, so its TLB is flushed
     * naturally by the CR3 reload when it is next scheduled. */
    pmap_remove_range(pmap, (uint32_t)va, (uint32_t)(va + len));
}

/*
 * Is a framebuffer client's mapping currently pointed at real video memory?
 *
 * This is what the console needs in order to decide whether it may paint:
 * a client that has been redirected to the offscreen shadow cannot reach
 * the visible screen any more, so the console owns it and must repaint.  A
 * client that is still onscreen — because it is in the foreground, or
 * because the redirect degraded gracefully when no shadow could be
 * allocated — means the console must stay off the framebuffer.
 */
int fb_client_onscreen(void) {
    int onscreen;

    spinlock_acquire(&fb_shadow_lock);
    onscreen = fb_client.active && !fb_is_offscreen;
    spinlock_release(&fb_shadow_lock);
    return onscreen;
}

/* Drop the registration when the owning process exits (called from the
 * graphics-release path).  Never touches the dying address space. */
void fb_client_clear(void *owner) {
    spinlock_acquire(&fb_shadow_lock);
    if (fb_client.active && fb_client.owner == owner) {
        fb_client.active = 0;
        fb_client.pager = NULL;
        fb_client.pmap = NULL;
        fb_is_offscreen = 0;
    }
    spinlock_release(&fb_shadow_lock);
}

/* ===== Linear /dev/fb0 shim for planar framebuffers (VGA/EGA) =====
 *
 * Planar VGA/EGA has no directly-mmap'able linear aperture: the 4 bit planes
 * overlap at one 64 KB window and a pixel is addressed through the Graphics
 * Controller.  A userspace mmap client (Xfbdev) needs a flat linear surface,
 * so for planar modes /dev/fb0 hands out an 8bpp colour-index RAM shim (one
 * byte/pixel, low nibble = the 16-colour index), and a periodic flush converts
 * it to the planes via the driver's batched blit_indexed.  The text console
 * and a graphics client never own the screen simultaneously (the KD_GRAPHICS
 * gate), so the console shadow and this shim never fight over the planes.
 */
static uint8_t   *fb_ufb_virt;   /* 8bpp linear shim (kernel VA)         */
static uintptr_t  fb_ufb_phys;   /* physical base (device-pager handle)  */
static size_t     fb_ufb_size;   /* allocation bytes (page-rounded)      */

static inline int fb_is_planar(void) { return fb.blit_indexed != NULL; }
static inline size_t fb_ufb_pitch(void) { return (size_t)fb.width; }
static inline size_t fb_ufb_bytes(void) { return (size_t)fb.width * (size_t)fb.height; }

static int fb_ufb_ensure(void) {
    size_t npages;
    void *v;

    if (fb_ufb_virt) return 1;
    if (!fb_is_planar() || fb.width == 0 || fb.height == 0) return 0;
    npages = (fb_ufb_bytes() + 0xFFFU) >> 12;
    v = pmm_alloc_contiguous(npages);
    if (!v) return 0;
    memset(v, 0, npages << 12);
    fb_ufb_virt = v;
    fb_ufb_phys = (uintptr_t)V2P(v);
    fb_ufb_size = npages << 12;
    return 1;
}

/* Convert the linear shim to the planes — the planar /dev/fb0 "scanout".
 * Called from the console tick while a graphics client owns the screen.
 * Safe in IRQ context: blit_indexed writes fb.addr (kernel-range direct map)
 * and reads the shim (kernel direct map), both mapped in every pmap. */
void fb_planar_present_userfb(void) {
    if (!fb_ufb_virt || !fb.blit_indexed) return;
    fb.blit_indexed(fb_ufb_virt, (uint32_t)fb_ufb_pitch(),
                    0, 0, fb.width, fb.height);
}

static void *fb_fs_mmap(fs_node_t *node, void *addr, size_t length, int prot, int flags, off_t offset) {
    vm_object_t *obj;
    vm_map_t *map;
    uintptr_t virt = (uintptr_t)addr;
    size_t aligned_length;
    uint32_t vm_prot = 0;

    /*
     * Use 64-bit math for the size and the bounds compare; otherwise a
     * crafted mode (e.g. via `vga=` cmdline) where pitch * height wraps
     * a uint32_t lets `offset + length > fb_size` admit any window into
     * physical RAM via /dev/fb0 mmap.
     */
    /* Planar framebuffers can't be mmap'd directly (the planes overlap a
     * 64 KB window); hand out the linear 8bpp shim instead and convert it to
     * the planes on flush.  Linear framebuffers map their real aperture. */
    int planar = fb_is_planar();
    uintptr_t base_phys;
    uint64_t fb_size;
    if (planar) {
        if (!fb_ufb_ensure()) return (void *)-1;
        base_phys = fb_ufb_phys;
        fb_size = (uint64_t)fb_ufb_bytes();
    } else {
        /* The device pager treats its handle as the physical base — its
         * vm_pager_device_phys returns handle+offset to the page-fault PTE
         * installer.  We MUST pass fb.phys here, not fb.addr (an ioremap VA
         * would install PTEs pointing at random RAM). */
        if (fb.phys == 0) return (void *)-1;
        base_phys = fb.phys;
        fb_size = (uint64_t)fb.pitch * (uint64_t)fb.height;
    }

    if (offset < 0 || length == 0) return (void *)-1;
    if ((uint64_t)offset > fb_size) return (void *)-1;
    if ((uint64_t)length > fb_size - (uint64_t)offset) return (void *)-1;
    if ((offset & 0xFFF) != 0) return (void *)-1;

    aligned_length = (length + 0xFFF) & ~0xFFFU;
    map = current_process->vm_map;
    if (!map) return (void *)-1;

    if (virt == 0 || !(flags & MAP_FIXED)) {
        if (vm_map_find_space(map, &virt, aligned_length) != 0) return (void *)-1;
    } else {
        if ((virt & 0xFFF) != 0) return (void *)-1;
        if (vm_map_remove(map, virt, virt + aligned_length) != 0) return (void *)-1;
    }

    if (prot & VM_PROT_READ)  vm_prot |= VM_PROT_READ;
    if (prot & VM_PROT_WRITE) vm_prot |= VM_PROT_WRITE;
    if (prot & VM_PROT_EXEC)  vm_prot |= VM_PROT_EXEC;
    vm_prot |= VM_PROT_USER;

    obj = vm_object_allocate(VM_OBJ_TYPE_DEVICE, aligned_length);
    if (!obj) return (void *)-1;
    obj->pager = vm_pager_allocate(VM_OBJ_TYPE_DEVICE,
                                   (void *)(base_phys + (uintptr_t)offset),
                                   aligned_length,
                                   vm_prot,
                                   0);
    if (!obj->pager) {
        vm_object_deallocate(obj);
        return (void *)-1;
    }
    /* The framebuffer is a linear pixel aperture, not strict MMIO
     * registers: map it write-combining so the X server's blits coalesce
     * into burst writes instead of one serialized uncached store per
     * pixel.  Falls back to uncached automatically when the CPU has no
     * PAT/WC (see vm_fault's device path). */
    vm_pager_set_cache_mode(obj->pager, VM_PAGER_CACHE_WC);
    if (vm_map_insert(map, obj, 0, virt, virt + aligned_length, vm_prot, vm_prot, VM_INHERIT_NONE) != 0) {
        vm_object_deallocate(obj);
        return (void *)-1;
    }

    /* Track this mapping so a VT switch can move a backgrounded X server's
     * framebuffer offscreen.  The physical base the pager carries is
     * fb.phys + offset; record `offset` so the redirect preserves it. */
    fb_client_register(map->pmap, virt, aligned_length, obj->pager,
                       (uintptr_t)offset, (void *)current_process);

    (void)node;
    return (void *)virt;
}

/* Per-device fb nodes for multi-framebuffer */
static fs_node_t fb_nodes[FB_MAX_DEVICES];

/* ==================== Video Subsystem / Registry ==================== */

void video_register_driver(video_driver_t *drv) {
    if (!drv) return;
    drv->next = video_drivers;
    video_drivers = drv;
}

/* External install functions for drivers (Since we lack constructors) */


/* Default Multiboot Driver Wrapper */
static multiboot_info_t *saved_mbi = NULL;
static void *saved_mbi_fb_addr = NULL;
static size_t saved_mbi_fb_len = 0;
static int mb_probe(void) {
    if (saved_mbi && (saved_mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO) &&
        saved_mbi->framebuffer_type != MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT) return 0;
    return -1;
}
static int mb_init(fb_info_t *info) {
    uint64_t fb_phys;

    if (!saved_mbi) return -1;
    if ((saved_mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO) == 0 ||
        saved_mbi->framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT) {
        return -1;
    }

    fb_phys = saved_mbi->framebuffer_addr;
    if (fb_phys > UINT32_MAX) {
        return -1;
    }
    if (saved_mbi_fb_addr == NULL) {
        saved_mbi_fb_len = (size_t)saved_mbi->framebuffer_pitch * saved_mbi->framebuffer_height;
        if (saved_mbi_fb_len == 0) {
            return -1;
        }
        saved_mbi_fb_addr = ioremap_wc((resource_size_t)fb_phys, saved_mbi_fb_len);
        if (saved_mbi_fb_addr == NULL) {
            return -1;
        }
    }

    info->addr = (uint32_t *)saved_mbi_fb_addr;
    info->phys = (uintptr_t)fb_phys;   /* multiboot reports physical base */
    info->width = saved_mbi->framebuffer_width;
    info->height = saved_mbi->framebuffer_height;
    info->pitch = saved_mbi->framebuffer_pitch;
    info->bpp = saved_mbi->framebuffer_bpp;
    info->virt_width = info->width;
    info->virt_height = info->height;
    info->red_offset = 0;
    info->red_length = 0;
    info->green_offset = 0;
    info->green_length = 0;
    info->blue_offset = 0;
    info->blue_length = 0;
    if (saved_mbi->framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_RGB) {
        info->red_offset = saved_mbi->framebuffer_red_field_position;
        info->red_length = saved_mbi->framebuffer_red_mask_size;
        info->green_offset = saved_mbi->framebuffer_green_field_position;
        info->green_length = saved_mbi->framebuffer_green_mask_size;
        info->blue_offset = saved_mbi->framebuffer_blue_field_position;
        info->blue_length = saved_mbi->framebuffer_blue_mask_size;
    }
    fb_set_default_color_layout(info);
    info->set_viewport = NULL;
    info->putpixel = linear_fb_putpixel;
    info->scroll = NULL;
    info->flush = NULL;
    info->blit_indexed = NULL;   /* linear: console shadow uses generic path */
    return 0;
}

static int mb_list_modes(struct video_mode_info *modes, int max_count) {
    if (!modes) return 1;
    if (max_count > 0) {
        modes[0].width = fb.width;
        modes[0].height = fb.height;
        modes[0].bpp = fb.bpp;
        modes[0].mode_id = 0;
        modes[0].flags = 0;
        return 1;
    }
    return 0;
}

static video_driver_t mb_driver = {
    .name = "multiboot",
    .priority = 10,
    .probe = mb_probe,
    .init = mb_init,
    .list_modes = mb_list_modes
};

/* ==================== Initialization ==================== */

/*
 * fb_register_devfs_node - Register /dev/fbN for a framebuffer device.
 */
static void fb_register_devfs_node(int index) {
    if (index < 0 || index >= FB_MAX_DEVICES) return;
    fs_node_t *node = &fb_nodes[index];
    memset(node, 0, sizeof(fs_node_t));

    /* Format name: "fb0", "fb1", etc. */
    node->name[0] = 'f';
    node->name[1] = 'b';
    if (index < 10) {
        node->name[2] = '0' + index;
        node->name[3] = '\0';
    } else {
        node->name[2] = '0' + (index / 10);
        node->name[3] = '0' + (index % 10);
        node->name[4] = '\0';
    }
    node->flags = FS_CHARDEVICE;
    node->ioctl = fb_fs_ioctl;
    node->mmap = fb_fs_mmap;
    node->read = fb_fs_read;
    node->write = fb_fs_write;
    devfs_register_device(node);
    kprintf("FB: Registered /dev/%s (%ux%u@%u)\n",
            node->name, fb_devices[index].width,
            fb_devices[index].height, fb_devices[index].bpp);
}

void fb_init(multiboot_info_t *mbi) {
    char vid_arg[64];
    char vga_arg[64];
    int have_video_arg = 0;
    int have_vga_arg = 0;
    uint32_t req_w = 0, req_h = 0, req_bpp = 0;

    saved_mbi = mbi;

    /* Parse command line early */
    if (cmdline_get("video", vid_arg, sizeof(vid_arg)) == 0)
        have_video_arg = 1;
    if (cmdline_get("vga", vga_arg, sizeof(vga_arg)) == 0)
        have_vga_arg = 1;

    /* Handle video=text/off/none early exits */
    if (have_video_arg) {
        if (strcmp(vid_arg, "text") == 0 ||
            strncmp(vid_arg, "text:", 5) == 0) {
            fb_active = 0;
            kprint("Video: text console selected.\n");
            return;
        }
        if (strcmp(vid_arg, "off") == 0 || strcmp(vid_arg, "none") == 0) {
            fb_active = 0;
            return;
        }
        if (strcmp(vid_arg, "ask") == 0) {
            if (video_ask_mode(&fb) == 1) {
                fb_active = 1;
                if (!fb.virt_width) fb.virt_width = fb.width;
                if (!fb.virt_height) fb.virt_height = fb.height;
                fb_console_init();
                fb_devices[0] = fb;
                fb_device_count = 1;
                fb_register_devfs_node(0);
                kprint("Video: VESA BIOS Mode Active.\n");
                return;
            } else {
                fb_active = 0;
                kprint("Video: Text mode selected.\n");
                return;
            }
        }
    }

    if (have_vga_arg && strcmp(vga_arg, "ask") == 0) {
        if (video_ask_mode(&fb) == 1) {
            fb_active = 1;
            if (!fb.virt_width) fb.virt_width = fb.width;
            if (!fb.virt_height) fb.virt_height = fb.height;
            fb_console_init();
            fb_devices[0] = fb;
            fb_device_count = 1;
            fb_register_devfs_node(0);
            kprint("Video: VESA BIOS Mode Active.\n");
            return;
        }

        fb_active = 0;
        kprint("Video: Text mode selected.\n");
        return;
    }
    
    /* Register all known drivers */
    video_register_driver(&mb_driver);
    #ifdef ENABLE_BGA
    bga_install();
    #endif
    bga_install();
    vga_install();

    /* video=show — dump every mode every registered driver claims to
     * support, then continue with normal selection.  Useful for
     * picking a value to feed back as vga=WxH@BPP. */
    if (have_video_arg && strcmp(vid_arg, "show") == 0) {
        struct video_mode_info modes[32];
        video_driver_t *drv = video_drivers;
        kprint("Video: available modes:\n");
        while (drv) {
            int probed = drv->probe ? drv->probe() : 0;
            kprintf("  %s%s:\n", drv->name,
                    probed == 0 ? "" : " (not present)");
            if (probed == 0 && drv->list_modes) {
                int total = drv->list_modes(NULL, 0);
                if (total > 32) total = 32;
                if (total > 0) {
                    int count = drv->list_modes(modes, total);
                    for (int i = 0; i < count; i++) {
                        kprintf("    %ux%u@%u (mode %u)\n",
                                modes[i].width, modes[i].height,
                                modes[i].bpp, modes[i].mode_id);
                    }
                }
            }
            drv = drv->next;
        }
    }

    /* Parse vga=WxH@BPP mode request */
    if (have_vga_arg) {
        if (fb_parse_vga_mode(vga_arg, &req_w, &req_h, &req_bpp) == 0) {
            kprintf("Video: Requested mode %ux%u", req_w, req_h);
            if (req_bpp)
                kprintf("@%u", req_bpp);
            kprint(" via vga= parameter.\n");
        } else {
            kprintf("Video: Invalid vga= parameter: '%s'\n", vga_arg);
            have_vga_arg = 0;
        }
    }

    /* Linux compat: video=WxH@BPP (or video=WxH) is the kernel
     * cmdline form most users type.  If video= didn't match any of
     * the keyword shortcuts above (text/off/none/ask) and looks
     * like a mode string, treat it as if vga= had been passed. */
    if (have_video_arg && !have_vga_arg &&
        req_w == 0 && req_h == 0) {
        if (fb_parse_vga_mode(vid_arg, &req_w, &req_h, &req_bpp) == 0) {
            kprintf("Video: Requested mode %ux%u", req_w, req_h);
            if (req_bpp)
                kprintf("@%u", req_bpp);
            kprint(" via video= parameter.\n");
            have_vga_arg = 1;
        }
    }

    /* Driver Selection Logic */
    video_driver_t *selected_drv = NULL;
    int selected_mode_id = -1;

    /* Pass 0: if the bootloader handed us a framebuffer via the MBI
     * (mb_driver's probe succeeds when MULTIBOOT_INFO_FRAMEBUFFER_INFO
     * is set and it's not text mode), inherit it as-is.  Skipped when
     * the user passed an explicit vga= or video= cmdline argument — in
     * that case the later passes pick a fresh mode and override. */
    if (!have_vga_arg && !have_video_arg && mb_probe() == 0) {
        selected_drv = &mb_driver;
        kprint("Video: Inheriting framebuffer from Multiboot info.\n");
    }

    /* Pass 1: vga=WxH@BPP mode-based selection */
    if (have_vga_arg && req_w > 0 && req_h > 0) {
        if (fb_find_matching_mode(req_w, req_h, req_bpp, &selected_drv, &selected_mode_id) == 0) {
            kprintf("Video: Found matching mode %d on driver '%s'.\n",
                    selected_mode_id, selected_drv->name);
        } else {
            kprintf("Video: No driver supports %ux%u", req_w, req_h);
            if (req_bpp)
                kprintf("@%u", req_bpp);
            kprint(", falling back.\n");
        }
    }
    
    /* Pass 2: video=<drivername> command line override */
    if (!selected_drv && have_video_arg) {
        video_driver_t *curr = video_drivers;
        while (curr) {
            if (strcmp(curr->name, vid_arg) == 0) {
                if (curr->probe() == 0) {
                    selected_drv = curr;
                    break;
                } else {
                     kprintf("Video: Requested driver '%s' not available.\n", vid_arg);
                }
            }
            curr = curr->next;
        }
    }
    
    /* Pass 3: Pick highest priority available if not selected yet */
    if (!selected_drv && (have_video_arg || have_vga_arg || !hw_text_active)) {
        video_driver_t *curr = video_drivers;
        int max_prio = -1;
        
        while (curr) {
            if (curr->probe() == 0) {
                if (curr->priority > max_prio) {
                    max_prio = curr->priority;
                    selected_drv = curr;
                }
            }
            curr = curr->next;
        }
    }

    if (!selected_drv) {
        kprint("Video: No suitable driver found.\n");
        fb_active = 0;
        return;
    }

    /* Initialize Selected Driver */
    kprintf("Video: Initializing driver: %s\n", selected_drv->name);
    if (selected_drv->init(&fb) != 0) {
        kprint("Video: Initialization failed.\n");
        fb_active = 0;
        return;
    }

    fb_active = 1;
    current_driver = selected_drv;

    /* If vga= requested a specific mode and the driver supports set_mode, switch now */
    if (selected_mode_id >= 0 && selected_drv->set_mode) {
        if (selected_drv->set_mode(selected_mode_id) == 0) {
            kprintf("Video: Mode %d set successfully.\n", selected_mode_id);
        } else {
            kprintf("Video: Failed to set mode %d, using default.\n", selected_mode_id);
        }
    }

    fb_set_default_color_layout(&fb);
    if (!fb.putpixel) fb.putpixel = linear_fb_putpixel;
    if (fb.virt_height == 0) fb.virt_height = fb.height;
    if (fb.virt_width == 0) fb.virt_width = fb.width;
    kprintf("Video: %ux%u@%u (pitch=%u)\n", fb.width, fb.height, fb.bpp, fb.pitch);

    /* Register as fb0 in multi-fb registry */
    fb_devices[0] = fb;
    fb_device_count = 1;

    /*
     * Take the console.  Reaching here means fb_active is 1: a linear
     * framebuffer is up and the caller did not ask for text mode -- video=text,
     * vga=text and the "no driver" paths all returned above with fb_active
     * cleared.
     *
     * This used to be conditional on `!hw_text_active || have_video_arg ||
     * have_vga_arg`, which looks like "don't steal the console from the VGA
     * text driver" but is unsatisfiable in the case that matters.
     * hw_text_init() sets hw_text_active unconditionally, so booting via GRUB
     * -- which hands us a framebuffer through the multiboot info and no vga=
     * argument -- left all three false and fb_console_init() was never called
     * at all.  The result was a console owned by hw_text: it wrote VGA text
     * cells to 0xB8000, which nothing displays in a graphics mode, and it left
     * the VT at its 80x25 geometry.  With GRUB's 800x600 that put the status
     * line 200 pixels up from the bottom and 160 short of the right edge, with
     * the rest of the screen never painted.
     *
     * If a way to keep the text console over a live framebuffer is ever wanted,
     * it belongs in the argument parsing above with the other text-mode
     * requests, where it can clear fb_active and mean something.
     */
    fb_console_init();


    /* Register /dev/fb0 */
    fb_register_devfs_node(0);
}
