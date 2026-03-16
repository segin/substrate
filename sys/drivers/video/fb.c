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
#include <sys/mman.h>
#include <sys/proc.h>
#include <vfs/vfs.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_pager.h>
#include <kern/resource.h>

#include <arch/i386/pmap.h>

#include <drivers/video/bga.h>
#include <drivers/video/fb.h>
#include <drivers/video/fb_console.h>
#include <drivers/video/font.h>

/* ==================== Global State ==================== */

fb_info_t fb;
int fb_active = 0;
extern int hw_text_active;

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
int fb_parse_vga_mode(const char *arg, uint32_t *width, uint32_t *height, uint32_t *bpp) {
    uint32_t w = 0, h = 0, b = 0;
    const char *p = arg;

    if (!arg || !width || !height || !bpp) return -1;

    /* Parse width */
    while (*p >= '0' && *p <= '9') {
        w = w * 10 + (*p - '0');
        p++;
    }
    if (w == 0 || (*p != 'x' && *p != 'X')) return -1;
    p++; /* skip 'x' */

    /* Parse height */
    while (*p >= '0' && *p <= '9') {
        h = h * 10 + (*p - '0');
        p++;
    }
    if (h == 0) return -1;

    /* Parse optional @BPP */
    if (*p == '@') {
        p++;
        while (*p >= '0' && *p <= '9') {
            b = b * 10 + (*p - '0');
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
        struct fb_var_screeninfo *vi = (struct fb_var_screeninfo *)arg;
        if (!vi) return -1;

        memset(vi, 0, sizeof(struct fb_var_screeninfo));
        vi->xres = fb.width;
        vi->yres = fb.height;
        vi->xres_virtual = fb.virt_width ? fb.virt_width : fb.width;
        vi->yres_virtual = fb.virt_height ? fb.virt_height : fb.height;
        vi->bits_per_pixel = fb.bpp;

        fb_set_default_color_layout(&fb);
        if (fb.red_length || fb.green_length || fb.blue_length) {
            vi->red.offset = fb.red_offset;
            vi->red.length = fb.red_length;
            vi->green.offset = fb.green_offset;
            vi->green.length = fb.green_length;
            vi->blue.offset = fb.blue_offset;
            vi->blue.length = fb.blue_length;
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
    vm_object_t *obj;
    vm_map_t *map;
    uintptr_t virt = (uintptr_t)addr;
    size_t aligned_length;
    uint32_t vm_prot = 0;

    uint32_t fb_size = fb.pitch * fb.height;
    if (offset + length > fb_size) return (void *)-1;
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
                                   (void *)((uintptr_t)fb.addr + (uintptr_t)offset),
                                   aligned_length,
                                   vm_prot,
                                   0);
    if (!obj->pager) {
        vm_object_deallocate(obj);
        return (void *)-1;
    }
    if (vm_map_insert(map, obj, 0, virt, virt + aligned_length, vm_prot, vm_prot, VM_INHERIT_NONE) != 0) {
        vm_object_deallocate(obj);
        return (void *)-1;
    }

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
    // kprint("Video: Registered driver: "); kprint(drv->name); kprint("\n");
}

/* External install functions for drivers (Since we lack constructors) */
extern void bga_install(void);
extern void vga_install(void);

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
        saved_mbi_fb_addr = ioremap((resource_size_t)fb_phys, saved_mbi_fb_len);
        if (saved_mbi_fb_addr == NULL) {
            return -1;
        }
    }

    info->addr = (uint32_t *)saved_mbi_fb_addr;
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
    
    /* Register all known drivers */
    video_register_driver(&mb_driver);
    #ifdef ENABLE_BGA
    bga_install();
    #endif
    bga_install();
    vga_install();

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

    /* Driver Selection Logic */
    video_driver_t *selected_drv = NULL;
    int selected_mode_id = -1;

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

    /* Register Console (unless hw_text active and not overridden) */
    if (!hw_text_active || have_video_arg || have_vga_arg) {
        fb_console_init();
    }
    
    /* Register /dev/fb0 */
    fb_register_devfs_node(0);
}
