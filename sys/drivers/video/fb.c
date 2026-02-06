/*
 * fb.c - Framebuffer Core Driver
 *
 * Initializes framebuffer from Multiboot, handles pixel operations,
 * and registers /dev/fb0 device node.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kern/cmdline.h>
#include <kern/console.h>
#include <sys/fb.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <vfs/vfs.h>

#include <arch/i386/pmap.h>

#include <drivers/video/bga.h>
#include <drivers/video/fb.h>
#include <drivers/video/fb_console.h>
#include <drivers/video/font.h>

/* ==================== Global State ==================== */

fb_info_t fb;
int fb_active = 0;
extern int hw_text_active;

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

static uint32_t fb_get_raw_pixel(uint32_t color, uint8_t bpp) {
    if (bpp >= 15) {
        switch (bpp) {
            case 32: return color;
            case 24: return color & 0xFFFFFF;
            case 16: return ((color >> 8) & 0xF800) | ((color >> 5) & 0x07E0) | ((color >> 3) & 0x001F);
            case 15: return ((color >> 9) & 0x7C00) | ((color >> 6) & 0x03E0) | ((color >> 3) & 0x001F);
        }
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

int video_set_viewport(int x, int y) {
    if (current_driver && current_driver->set_viewport) {
        return current_driver->set_viewport(x, y);
    }
    return -1;
}

static int fb_fs_ioctl(fs_node_t *node, uint32_t request, void *arg) {
    (void)node;
    if (request == FBIOGET_VSCREENINFO) {
        struct fb_var_screeninfo *vi = (struct fb_var_screeninfo *)arg;
        if (!vi) return -1;

        memset(vi, 0, sizeof(struct fb_var_screeninfo));
        vi->xres = fb.width;
        vi->yres = fb.height;
        vi->xres_virtual = fb.virt_width ? fb.virt_width : fb.width;
        vi->yres_virtual = fb.virt_height ? fb.virt_height : fb.height;
        vi->bits_per_pixel = fb.bpp;

        if (fb.bpp == 32 || fb.bpp == 24) {
            vi->red.offset = 16; vi->red.length = 8;
            vi->green.offset = 8; vi->green.length = 8;
            vi->blue.offset = 0; vi->blue.length = 8;
        } else if (fb.bpp == 16) {
            vi->red.offset = 11; vi->red.length = 5;
            vi->green.offset = 5; vi->green.length = 6;
            vi->blue.offset = 0; vi->blue.length = 5;
        }

        return 0;
    } else if (request == FBIOGET_VIDEO_MODES) {
        if (!current_driver || !current_driver->list_modes) return -1;
        
        struct video_mode_query *query = (struct video_mode_query *)arg;
        if (!query) return -1;

        int total = current_driver->list_modes(NULL, 0);

        if (query->modes) {
            current_driver->list_modes(query->modes, query->count);
        }

        query->count = total;
        return 0;
    } else if (request == FBIOPUT_VIDEO_MODE) {
        if (!current_driver || !current_driver->set_mode) return -1;
        uint32_t mode_id = (uint32_t)(uintptr_t)arg; /* Mode ID passed directly as arg or pointer? Usually pointer to int. */
        /* Let's assume pointer to uint32_t */
        if (!arg) return -1;
        mode_id = *(uint32_t*)arg;
        
        if (current_driver->set_mode(mode_id) == 0) {
            /* Mode set success, fb info needs refresh? Driver init usually refreshes it. */
            /* Maybe call init again logic? set_mode should allow updating `fb` global. */
            return 0;
        }
        return -1;
    }
    return -1;
}

static void *fb_fs_mmap(fs_node_t *node, void *addr, size_t length, int prot, int flags, off_t offset) {
    (void)node; (void)prot; (void)flags;

    uint32_t fb_size = fb.pitch * fb.height;
    if (offset + length > fb_size) return (void *)-1;

    uintptr_t phys_base = (uintptr_t)fb.addr;
    uintptr_t phys = phys_base + (uintptr_t)offset;
    uintptr_t virt = (uintptr_t)addr;

    uint32_t pte_flags = PTE_P | PTE_U | PTE_W;

    for (size_t i = 0; i < length; i += 0x1000) {
        pmap_enter(current_process->pmap, virt + i, phys + i, 0, pte_flags);
    }

    return addr;
}

static fs_node_t fb_node;

/* ==================== Video Subsystem / Registry ==================== */

static video_driver_t *video_drivers = NULL;

void video_register_driver(video_driver_t *drv) {
    if (!drv) return;
    drv->next = video_drivers;
    video_drivers = drv;
    // kprint("Video: Registered driver: "); kprint(drv->name); kprint("\n");
}

/* External install functions for drivers (Since we lack constructors) */
extern void bga_install(void);
extern void vga_install(void);

/* Default Multiboot Driver Wrapper */
static multiboot_info_t *saved_mbi = NULL;
static int mb_probe(void) {
    if (saved_mbi && (saved_mbi->flags & (1 << 12))) return 0;
    return -1;
}
static int mb_init(fb_info_t *info) {
    if (!saved_mbi) return -1;
    info->addr = (uint32_t *)(uintptr_t)saved_mbi->framebuffer_addr;
    info->width = saved_mbi->framebuffer_width;
    info->virt_width = info->width;
    info->virt_height = info->height;
    info->set_viewport = NULL;
    info->putpixel = linear_fb_putpixel;
    info->scroll = NULL;
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

void fb_init(multiboot_info_t *mbi) {
    char vid_arg[32];
    if (cmdline_get("video", vid_arg, 32) == 0) {
        if (strcmp(vid_arg, "off") == 0 || strcmp(vid_arg, "none") == 0) {
            fb_active = 0;
            return;
        }
        if (strcmp(vid_arg, "ask") == 0) {
            if (video_ask_mode(&fb) == 1) {
                fb_active = 1; /* VESA Mode Set successfully via BIOS */
                /* fb struct populated by video_ask_mode */

                /* Ensure virtual dimensions are set if missing */
                if (!fb.virt_width) fb.virt_width = fb.width;
                if (!fb.virt_height) fb.virt_height = fb.height;
                
                /* Register console */
                fb_console_init();
                
                /* Register FB device */
                memset(&fb_node, 0, sizeof(fs_node_t));
                strcpy(fb_node.name, "fb0");
                fb_node.flags = FS_CHARDEVICE;
                fb_node.ioctl = fb_fs_ioctl;
                fb_node.mmap = fb_fs_mmap;
                devfs_register_device(&fb_node);
                
                kprint("Video: VESA BIOS Mode Active.\n");
                return;
            } else {
                /* Text mode selected */
                fb_active = 0;
                /* hw_text is likely active from kmain init, ensure it knows we want it. */
                kprint("Video: Text mode selected.\n");
                return; /* Logic below will try mb_driver etc, which might fail or override. */
                /* If text mode selected, we just return. hw_text_init was called in kmain before fb_init. */
            }
        }
    }

    saved_mbi = mbi;
    
    /* Register Drivers */
    video_register_driver(&mb_driver);
    
    /* Install other known drivers */
    /* In a dynamic system, these would self-register or be scanned */
    #ifdef ENABLE_BGA
    bga_install();
    #endif
    bga_install(); // Always include for now
    vga_install();
    int use_cmdline = 0;
    if (cmdline_get("video", vid_arg, 32) == 0) {
        use_cmdline = 1;
    }

    /* Driver Selection Logic */
    video_driver_t *best_drv = NULL;
    video_driver_t *selected_drv = NULL;
    
    /* Pass 1: Check command line override */
    if (use_cmdline) {
        video_driver_t *curr = video_drivers;
        while (curr) {
            if (strcmp(curr->name, vid_arg) == 0) {
                if (curr->probe() == 0) {
                    selected_drv = curr;
                    break;
                } else {
                     kprint("Video: Requested driver '");
                     kprint(vid_arg);
                     kprint("' not available.\n");
                }
            }
            curr = curr->next;
        }
    }
    
    /* Pass 2: Pick highest priority available if not selected yet */
    if (!selected_drv && (use_cmdline || !hw_text_active)) {
        video_driver_t *curr = video_drivers;
        int max_prio = -1;
        
        while (curr) {
            if (curr->probe() == 0) {
                if (curr->priority > max_prio) {
                    max_prio = curr->priority;
                    best_drv = curr;
                }
            }
            curr = curr->next;
        }
        selected_drv = best_drv;
    }

    if (!selected_drv) {
        kprint("Video: No suitable driver found.\n");
        fb_active = 0;
        return;
    }

    /* Initialize Selected Driver */
    kprint("Video: Initializing driver: "); kprint(selected_drv->name); kprint("\n");
    if (selected_drv->init(&fb) == 0) {
        fb_active = 1;
        current_driver = selected_drv;
        fb.putpixel = fb.putpixel ? fb.putpixel : linear_fb_putpixel; // Ensure fallback
        if (fb.virt_height == 0) fb.virt_height = fb.height; // Fallback
        kprint("Video: Initialization success.\n");
    } else {
        kprint("Video: Initialization failed.\n");
        fb_active = 0;
        return;
    }

    /* Register Console (unless hw_text active and not overridden) */
    /* Logic: If user specifically asked for video=xxx, we assume they want FB console
       even if hw_text was active. If we just fell back to multiboot/default, and hw_text
       is active, maybe keep text?
       Current logic: If hw_text_active, don't clobber unless cmdline requested video.
    */
    int register_console = 1;
    if (hw_text_active && !use_cmdline) {
        // If we are just using default (e.g. multiboot) but we have text mode working,
        // we might prefer text mode for speed/stability unless explicit override?
        // Actually, if we have FB, we probably want FB console?
        // Previous logic: if (hw_text_active && cmdline != 0) -> skip.
        // Wait, previous logic was: "Register console if hw_text is not active OR video override present"
        // So:
        register_console = (!hw_text_active) || use_cmdline;
    }

    if (register_console) {
        fb_console_init();
    }
    
    /* Register /dev/fb0 */
    memset(&fb_node, 0, sizeof(fs_node_t));
    strcpy(fb_node.name, "fb0");
    fb_node.flags = FS_CHARDEVICE;
    fb_node.ioctl = fb_fs_ioctl;
    fb_node.mmap = fb_fs_mmap;
    devfs_register_device(&fb_node);

    kprint("FB: Initialized /dev/fb0.\n");
}
