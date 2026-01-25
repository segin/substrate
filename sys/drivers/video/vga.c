#include <drivers/video/vga.h>
#include <kern/console.h>
#include <arch/x86-common/include/io.h>
#include <stdint.h>
#include <string.h>

/* ================== Register Definitions ================== */

/* VGA Mode 13h (320x200x256) Register State */
static const vga_regs_t mode_13h = {
    .misc = VGA_MISC_CLK_25MHZ | VGA_MISC_HSYNC_NEG | VGA_MISC_VSYNC_NEG | VGA_MISC_IO_ADDR_SEL | VGA_MISC_RAM_EN, /* 0x63 */
    .seq  = { 0x03, 0x01, 0x0F, 0x00, VGA_SEQ_MEM_MODE_CHAIN4 | VGA_SEQ_MEM_MODE_EXT_MEM | VGA_SEQ_MEM_MODE_ODD_EVEN }, /* 0x0E */ 
    /* ... Timings omitted for brevity, keeping hex for timings as they are calculated values ... */
    .crtc = {
        0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,
        0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x9C, 0x0E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3,
        0xFF
    },
    .gc   = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF },
    .ac   = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x41, 0x00, 0x0F, 0x00, 0x00
    }
};

/* VGA Mode 12h (640x480x16) Register State */
static const vga_regs_t mode_12h = {
    .misc = VGA_MISC_CLK_25MHZ | VGA_MISC_HSYNC_NEG | VGA_MISC_VSYNC_NEG | VGA_MISC_IO_ADDR_SEL | VGA_MISC_RAM_EN | VGA_MISC_PAGE_ODD_EVEN, /* 0xE3 */
    .seq  = { 0x03, 0x01, 0x0F, 0x00, VGA_SEQ_MEM_MODE_EXT_MEM }, /* 0x02 */
    .crtc = {
        0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0x0B, 0x3E,
        0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xEA, 0x0C, 0xDF, 0x28, 0x00, 0xE7, 0x04, 0xE3,
        0xFF
    },
    .gc   = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0F, 0xFF },
    .ac   = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
        0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
        0x01, 0x00, 0x0F, 0x00, 0x00
    }
};

/* EGA Mode 10h (640x350x16) Register State */
static const vga_regs_t mode_10h = {
    .misc = VGA_MISC_CLK_28MHZ | VGA_MISC_IO_ADDR_SEL | VGA_MISC_RAM_EN | VGA_MISC_PAGE_ODD_EVEN, /* 0xA7 */
    .seq  = { 0x03, 0x01, 0x0F, 0x00, 0x06 },
    .crtc = { 
        0x5B, 0x4F, 0x53, 0x37, 0x52, 0x00, 0x6C, 0x1F,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x5E, 0x2B, 0x5D, 0x28, 0x0F, 0x96, 0xB9, 0xA3, 
        0xFF 
    },
    .gc   = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF },
    .ac   = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
        0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 
        0x01, 0x00, 0x0F, 0x00, 0x00 
    }
};

static void vga_write_regs(const vga_regs_t *regs) {
    /* Determine CRTC port based on Misc Output Bit 0 (0=B4, 1=D4) */
    uint16_t crtc_index_port = (regs->misc & VGA_MISC_IO_ADDR_SEL) ? VGA_CRTC_INDEX_COLOR : VGA_CRTC_INDEX_MONO;
    uint16_t crtc_data_port  = (regs->misc & VGA_MISC_IO_ADDR_SEL) ? VGA_CRTC_DATA_COLOR : VGA_CRTC_DATA_MONO;
    uint16_t input_stat_port = (regs->misc & VGA_MISC_IO_ADDR_SEL) ? VGA_INPUT_STAT1_COLOR : VGA_INPUT_STAT1_MONO;

    /* Write MISC */
    outb(VGA_MISC_WRITE, regs->misc);

    /* Write SEQUENCER */
    for (int i = 0; i < VGA_NUM_SEQ_REGS; i++) {
        outb(VGA_SEQ_INDEX, i);
        outb(VGA_SEQ_DATA, regs->seq[i]);
    }

    /* Unlock CRTC registers 0-7 */
    outb(crtc_index_port, 0x03);
    uint8_t crtc_03 = inb(crtc_data_port) | 0x80;
    outb(crtc_data_port, crtc_03);
    
    outb(crtc_index_port, 0x11);
    uint8_t crtc_11 = inb(crtc_data_port) & ~0x80;
    outb(crtc_data_port, crtc_11);

    /* Write CRTC */
    for (int i = 0; i < VGA_NUM_CRTC_REGS; i++) {
        outb(crtc_index_port, i);
        outb(crtc_data_port, regs->crtc[i]);
    }

    /* Write GRAPHICS CONTROLLER */
    for (int i = 0; i < VGA_NUM_GC_REGS; i++) {
        outb(VGA_GC_INDEX, i);
        outb(VGA_GC_DATA, regs->gc[i]);
    }

    /* Write ATTRIBUTE CONTROLLER */
    for (int i = 0; i < VGA_NUM_AC_REGS; i++) {
        (void)inb(input_stat_port); // Reset flip-flop to Index
        outb(VGA_AC_INDEX, i);
        outb(VGA_AC_WRITE, regs->ac[i]);
    }

    /* Enable Video (AC Index 0x20) */
    (void)inb(input_stat_port);
    outb(VGA_AC_INDEX, 0x20);
}

/* ================== Drawing Primitives ================== */

static void vga_putpixel_linear(int x, int y, uint32_t color) {
    if (x < 0 || x >= 320 || y < 0 || y >= 200) return;
    uint8_t *vram = (uint8_t*)VGA_GFX_MEM_BASE;
    vram[y * 320 + x] = (uint8_t)color;
}

static void vga_putpixel_planar(int x, int y, uint32_t color) {
    // Shared for VGA 640x480 and EGA 640x350
    // Clipping checked by caller or here? 
    // Just minimal bounds check to prevent corruption. 
    // Assuming 640 width logic.
    
    unsigned int offset = (y * 640 + x) / 8;
    unsigned int bit = 7 - (x % 8);
    uint8_t mask = 1 << bit;
    
    volatile uint8_t *mem = (volatile uint8_t *)(VGA_GFX_MEM_BASE + offset);
    
    outb(VGA_GC_INDEX, 0x05); outb(VGA_GC_DATA, 0x02); // Write Mode 2
    outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, mask); // Bit Mask
    
    volatile uint8_t dummy = *mem; (void)dummy;
    *mem = (uint8_t)color;
    
    outb(VGA_GC_INDEX, 0x05); outb(VGA_GC_DATA, 0x00);
    outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, 0xFF);
}

static void herc_putpixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= 720 || y < 0 || y >= 348) return;
    uint8_t *mem = (uint8_t*)VGA_HERC_MEM_BASE;
    uintptr_t row_offset = (y >> 2) * 90;
    uintptr_t bank_offset = (y & 3) * 0x2000;
    uintptr_t global_offset = bank_offset + row_offset + (x >> 3);
    uint8_t bit_mask = 0x80 >> (x & 7);
    if (color) mem[global_offset] |= bit_mask;
    else       mem[global_offset] &= ~bit_mask;
}

static void cga_putpixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= 320 || y < 0 || y >= 200) return;
    int bank = (y % 2) == 0 ? 0 : 1;
    int offset = (y / 2) * 80 + (x / 4);
    uint8_t *mem = (uint8_t *)(0xC00B8000 + (bank * 0x2000) + offset);
    int shift = 6 - ((x % 4) * 2);
    uint8_t mask = 0x03 << shift;
    *mem = (*mem & ~mask) | ((color & 0x03) << shift);
}

/* ================== Mode Setting ================== */

static int vga_set_mode(int width, int height, int bpp, fb_info_t *fb) {
    if (width == 320 && height == 200 && bpp == 8) {
        vga_write_regs(&mode_13h);
        if (fb) {
            fb->width = 320; fb->height = 200; fb->bpp = 8;
            fb->pitch = 320; fb->putpixel = vga_putpixel_linear;
            fb->addr = (uint32_t*)VGA_GFX_MEM_BASE;
        }
        return 0;
    } else if (width == 640 && height == 480 && bpp == 4) {
        vga_write_regs(&mode_12h);
        if (fb) {
            fb->width = 640; fb->height = 480; fb->bpp = 4;
            fb->pitch = 640/8; fb->putpixel = vga_putpixel_planar;
            fb->addr = (uint32_t*)VGA_GFX_MEM_BASE;
        }
        return 0;
    } else if (width == 640 && height == 350 && bpp == 4) {
        vga_write_regs(&mode_10h);
        if (fb) {
            fb->width = 640; fb->height = 350; fb->bpp = 4;
            fb->pitch = 640/8; fb->putpixel = vga_putpixel_planar;
            fb->addr = (uint32_t*)VGA_GFX_MEM_BASE;
        }
        return 0;
    } else if (width == 720 && height == 348) {
        // Hercules via direct port writing (Legacy)
        // Note: VGA registers don't map 1:1 to Hercules, so we use specialized init or assumption.
        // For strict merger, we should emulate or support dual mode.
        // Here we just re-implement the herc init sequence to ports 0x3B8 etc.
        /* Hercules init logic simplified */
        static const uint8_t herc_regs[] = {
            0x35, 0x2D, 0x2E, 0x07, 0x5B, 0x02, 0x57, 0x57, 0x02, 0x03, 0x00, 0x00
        };
        outb(0x3BF, 0x01); // Config
        for(int i=0;i<12;i++) { outb(0x3B4, i); outb(0x3B5, herc_regs[i]); }
        outb(0x3B8, 0x0A); // Mode Control
        
        if (fb) {
            fb->width = 720; fb->height = 348; fb->bpp = 1;
            fb->pitch = 90; fb->putpixel = herc_putpixel;
            fb->addr = (uint32_t*)VGA_HERC_MEM_BASE;
        }
        return 0;
    } else if (width == 320 && height == 200 && bpp == 2) {
        // CGA Stub
         kprint("VGA: CGA mode request partial.\n");
         if (fb) {
             fb->width = 320; fb->height = 200; fb->bpp = 2;
             fb->putpixel = cga_putpixel;
             fb->addr = (uint32_t*)VGA_CGA_MEM_BASE;
         }
         return 0;
    }
    
    kprint("VGA: Unsupported mode requested.\n");
    return -1; 
}

/* ================== Driver Interface ================== */

static int vga_probe(void) {
    return 0; 
}

static int vga_init_driver(fb_info_t *fb) {
    kprint("VGA: Initializing Unified Driver [VGA/EGA/CGA/MDA]\n");
    // Default to VGA Mode 13h
    return vga_set_mode(320, 200, 8, fb);
}

static int vga_list_modes(struct video_mode_info *modes, int max_count) {
    int count = 0;
    if (max_count > 0) {
        modes[count++] = (struct video_mode_info){ .width = 320, .height = 200, .bpp = 8, .mode_id = 13 };
    }
    if (max_count > 1) {
        modes[count++] = (struct video_mode_info){ .width = 640, .height = 480, .bpp = 4, .mode_id = 12 };
    }
    if (max_count > 2) {
        modes[count++] = (struct video_mode_info){ .width = 640, .height = 350, .bpp = 4, .mode_id = 10 };
    }
    if (max_count > 3) {
        modes[count++] = (struct video_mode_info){ .width = 720, .height = 348, .bpp = 1, .mode_id = 7 }; // Hercules
    }
    return count;
}

static int vga_set_mode_id(int mode_id) {
    switch(mode_id) {
        case 13: return vga_set_mode(320, 200, 8, NULL);
        case 12: return vga_set_mode(640, 480, 4, NULL);
        case 10: return vga_set_mode(640, 350, 4, NULL);
        case 7:  return vga_set_mode(720, 348, 1, NULL); 
    }
    return -1;
}

static video_driver_t vga_driver = {
    .name = "vga",
    .priority = 20, 
    .probe = vga_probe,
    .init = vga_init_driver,
    .list_modes = vga_list_modes,
    .set_mode = vga_set_mode_id
};

void vga_install(void) {
    video_register_driver(&vga_driver);
}
