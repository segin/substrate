/*
 * fb.c - Framebuffer Core Driver
 *
 * Initializes framebuffer from Multiboot, handles pixel operations,
 * and registers /dev/fb0 device node.
 */

#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <kern/console.h>
#include <kern/cmdline.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/fb.h>
#include <vfs/vfs.h>
#include <arch/i386/pmap.h>
#include <drivers/video/fb.h>
#include <drivers/video/fb_console.h>
#include <drivers/video/font.h>
#include <drivers/video/bga.h>
#include <drivers/video/hercules.h>

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

static void linear_fb_putpixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= (int)fb.width || y < 0 || y >= (int)fb.height) return;

    if (fb.bpp >= 15) {
        uint8_t *pixel = (uint8_t *)((uintptr_t)fb.addr + y * fb.pitch + x * (fb.bpp / 8));
        switch (fb.bpp) {
            case 32:
                *(uint32_t *)pixel = color;
                break;
            case 24:
                pixel[0] = color & 0xFF;
                pixel[1] = (color >> 8) & 0xFF;
                pixel[2] = (color >> 16) & 0xFF;
                break;
            case 16: {
                uint16_t c = ((color >> 8) & 0xF800) |
                             ((color >> 5) & 0x07E0) |
                             ((color >> 3) & 0x001F);
                *(uint16_t *)pixel = c;
                break;
            }
            case 15: {
                uint16_t c = ((color >> 9) & 0x7C00) |
                             ((color >> 6) & 0x03E0) |
                             ((color >> 3) & 0x001F);
                *(uint16_t *)pixel = c;
                break;
            }
        }
    } else {
        /* Packed pixel formats (1, 2, 4, 8 bpp) with Palette Adaptation */
        uint8_t index = rgb_to_index(color);
        
        if (fb.bpp == 8) {
             uint8_t *pixel = (uint8_t *)((uintptr_t)fb.addr + y * fb.pitch + x);
             *pixel = index;
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
    for (uint32_t y = 0; y < fb.height; y++) {
        for (uint32_t x = 0; x < fb.width; x++) {
            fb_putpixel(x, y, color);
        }
    }
}

/* ==================== Device Node Operations ==================== */

static int fb_fs_ioctl(fs_node_t *node, uint32_t request, void *arg) {
    (void)node;
    if (request == FBIOGET_VSCREENINFO) {
        struct fb_var_screeninfo *vi = (struct fb_var_screeninfo *)arg;
        if (!vi) return -1;

        memset(vi, 0, sizeof(struct fb_var_screeninfo));
        vi->xres = fb.width;
        vi->yres = fb.height;
        vi->xres_virtual = fb.width;
        vi->yres_virtual = fb.height;
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

/* ==================== Initialization ==================== */

void fb_init(multiboot_info_t *mbi) {
    if (!(mbi->flags & (1 << 12))) {
        kprint("FB: Multiboot info has no framebuffer.\n");
        fb_active = 0;
        return;
    }

    fb.addr = (uint32_t *)(uintptr_t)mbi->framebuffer_addr;
    fb.width = mbi->framebuffer_width;
    fb.height = mbi->framebuffer_height;
    fb.pitch = mbi->framebuffer_pitch;
    fb.bpp = mbi->framebuffer_bpp;

    /* Check for native driver override via command line */
    char vid[32];
    if (cmdline_get("video", vid, 32) == 0) {
        if (strcmp(vid, "bga") == 0) {
            kprint("FB: video=bga detected. Attempting BGA initialization...\n");
            fb_info_t bga_fb;
            if (bga_init(&bga_fb) == 0) {
                fb = bga_fb;
                kprint("FB: Switched to BGA framebuffer.\n");
            } else {
                kprint("FB: BGA initialization failed. Falling back to Multiboot FB.\n");
            }
        } else if (strcmp(vid, "hercules") == 0) {
            fb_info_t herc_fb;
            if (herc_init(&herc_fb) == 0) {
                fb = herc_fb;
                kprint("FB: Switched to Hercules framebuffer.\n");
            }
        }
    }

    fb_active = 1;
    fb.putpixel = linear_fb_putpixel;

    /* Register console if hw_text is not active or video override present */
    int register_console = 1;
    if (hw_text_active && cmdline_get("video", vid, 32) != 0) {
        register_console = 0;
        kprint("FB: Hardware text mode active, skipping FB console registration.\n");
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

    kprint("FB: Initialized framebuffer and /dev/fb0.\n");
}
