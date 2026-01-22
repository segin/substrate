#include "fb.h"
#include "font.h"
#include "font.h"
#include <kern/console.h>
#include <sys/fb.h>
#include <sys/file.h>
#include "../../vfs/vfs.h"
#include "../../arch/i386/pmap.h"
#include <sys/proc.h>
#include <string.h>
#include <stddef.h>

static fb_info_t fb;
static int cursor_x = 0;
static int cursor_y = 0;
int fb_active = 0;

static console_backend_t fb_console_backend = {
    .name = "framebuffer",
    .write = fb_write,
    .putchar = NULL, // fb_write handles it
    .clear = NULL // fb_clear requires a color, console_backend clear is void. We can stub it.
};

static void fb_console_clear(void) {
    fb_clear(0x00000000); // Clear to black
}

// FS Node Operations
static int fb_fs_ioctl(fs_node_t *node, uint32_t request, void *arg) {
    (void)node;
    if (request == FBIOGET_VSCREENINFO) {
        struct fb_var_screeninfo *vi = (struct fb_var_screeninfo *)arg;
        if (!vi) return -1; // EFAULT
        
        memset(vi, 0, sizeof(struct fb_var_screeninfo));
        vi->xres = fb.width;
        vi->yres = fb.height;
        vi->xres_virtual = fb.width;
        vi->yres_virtual = fb.height;
        vi->bits_per_pixel = fb.bpp;
        
        // Standard RGB offsets for 32/24 bit
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
    return -1; // EINVAL
}

static void *fb_fs_mmap(fs_node_t *node, void *addr, size_t length, int prot, int flags, off_t offset) {
    (void)node; (void)prot; (void)flags;

    // Validate size
    uint32_t fb_size = fb.pitch * fb.height;
    if (offset + length > fb_size) return (void *)-1;

    uintptr_t phys_base = (uintptr_t)fb.addr; // Assuming fb.addr holds physical base from Multiboot
    // Note: If fb.addr was remapped, we'd need the original physical address here.
    // Since we access fb.addr directly in kernel, it assumes identity mapping or we need to fix kernel access too.
    
    uintptr_t phys = phys_base + (uintptr_t)offset;
    uintptr_t virt = (uintptr_t)addr;
    
    // Map with Cache Disable (PCD) for MMIO? 
    // Framebuffers can be cached if Write-Combining (WC) is supported (PAT), but standard caching is risky without flushing.
    // For safety, let's use PCD (uncached) or at least Write-Through (PWT). 
    // However, software rendering to uncached memory is slow.
    // For now, let's try standard mapping (allocator is 4K, assume MTRR handles WC or we just suffer generic caching issues/speed).
    // Actually, usually we want WC. Without PAT setup, we default to PWT or similar. 
    // Let's use PTE_P | PTE_U | PTE_W.
    
    uint32_t pte_flags = PTE_P | PTE_U | PTE_W; // User writable
    // If we had PTE_PCD, we could OR it: pte_flags |= PTE_PCD;

    for (size_t i = 0; i < length; i += 0x1000) {
        pmap_enter(current_process->pmap, virt + i, phys + i, 0, pte_flags);
    }

    return addr;
}

static fs_node_t fb_node;

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

    // Check for native driver override via command line
    #include "../../kern/cmdline.h"
    #include "bga.h"
    
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
        }
    }

    fb_active = 1;
    
    // Map framebuffer if needed? 
    // Usually Multiboot FB address is physical. 
    // In higher half kernel, we identity mapped lower 4MB. 
    // The FB likely resides high in physical memory (e.g. 0xE0000000).
    // We MUST map it.
    // However, our `pmap` might not be initialized fully yet if called too early?
    // `pmap_bootstrap` is called later in `kmain`.
    // IF we call `fb_init` after `pmap_bootstrap`, we can map it.
    // For now, let's assume identity map or map it ourselves.
    // BUT `fb.addr` is currently physical pointer. Accessing it will fault if not mapped.
    // Let's defer registration or mapping?
    // Actually, `kmain` calls `pmap_bootstrap` at line 306.
    // `vga_init` is called at line 175 (before pmap!). 
    // VGA memory 0xB8000 is within the first 4MB usually identity mapped by `boot.S`.
    // Framebuffer is likely NOT.
    // So `fb_init` MUST be called AFTER `pmap_bootstrap`.
    
    fb_console_backend.clear = fb_console_clear;
    console_register(&fb_console_backend);
    
    // Register /dev/fb0
    memset(&fb_node, 0, sizeof(fs_node_t));
    strcpy(fb_node.name, "fb0");
    fb_node.flags = FS_CHARDEVICE;
    fb_node.ioctl = fb_fs_ioctl;
    fb_node.mmap = fb_fs_mmap;
    devfs_register_device(&fb_node);
    
    kprint("FB: Initialized framebuffer and /dev/fb0.\n");
}

void fb_putpixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= (int)fb.width || y < 0 || y >= (int)fb.height) return;
    
    uint8_t *pixel = (uint8_t *)((uintptr_t)fb.addr + y * fb.pitch + x * (fb.bpp / 8));
    
    switch (fb.bpp) {
        case 32:
            *(uint32_t *)pixel = color;
            break;
        case 24:
            pixel[0] = color & 0xFF; // Blue
            pixel[1] = (color >> 8) & 0xFF; // Green
            pixel[2] = (color >> 16) & 0xFF; // Red
            break;
        case 16:
            // 5-6-5 format usually
            {
                uint16_t c = ((color >> 8) & 0xF800) |
                             ((color >> 5) & 0x07E0) |
                             ((color >> 3) & 0x001F);
                *(uint16_t *)pixel = c;
            }
            break;
        case 15:
            // 5-5-5 format
            {
                uint16_t c = ((color >> 9) & 0x7C00) |
                             ((color >> 6) & 0x03E0) |
                             ((color >> 3) & 0x001F);
                *(uint16_t *)pixel = c;
            }
            break;
        case 8:
             // Palette index? Just write the lower byte of color
             *pixel = color & 0xFF;
             break;
        default:
            // Unsupported
            break;
    }
}

void fb_clear(uint32_t color) {
    for (uint32_t y = 0; y < fb.height; y++) {
        for (uint32_t x = 0; x < fb.width; x++) {
            fb_putpixel(x, y, color);
        }
    }
    cursor_x = 0;
    cursor_y = 0;
}

void fb_putc(char c, uint32_t fg, uint32_t bg) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y += 16;
    } else if (c == '\r') {
        cursor_x = 0;
    } else {
        if (c >= 32 && c <= 126) {
            const uint8_t *glyph = font_8x16[c - 32];
            for (int y = 0; y < 16; y++) {
                for (int x = 0; x < 8; x++) {
                    if (glyph[y] & (0x80 >> x)) {
                        fb_putpixel(cursor_x + x, cursor_y + y, fg);
                    } else if (bg != 0xFFFFFFFF) { // Transparent bg if FFFFFFFF
                        fb_putpixel(cursor_x + x, cursor_y + y, bg);
                    }
                }
            }
        }
        cursor_x += 8;
    }

    if (cursor_x >= (int)fb.width) {
        cursor_x = 0;
        cursor_y += 16;
    }

    if (cursor_y + 16 > (int)fb.height) {
        // Scroll up by 16 pixels
        extern void *memcpy(void *dest, const void *src, size_t n);
        void *dst = fb.addr;
        void *src = (void*)((uintptr_t)fb.addr + 16 * fb.pitch);
        size_t size = (fb.height - 16) * fb.pitch;
        memcpy(dst, src, size);
        
        // Clear bottom 16 pixels
        for (uint32_t y = fb.height - 16; y < fb.height; y++) {
            for (uint32_t x = 0; x < fb.width; x++) {
                fb_putpixel(x, y, bg != 0xFFFFFFFF ? bg : 0);
            }
        }
        cursor_y -= 16;
    }
}

void fb_write(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        fb_putc(s[i], 0x00FFFFFF, 0x00000000); // White on Black
    }
}
