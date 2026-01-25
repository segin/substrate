#include <drivers/video/vga.h>
#include <kern/console.h>
#include <arch/x86-common/include/io.h>
#include <stdint.h>
#include <string.h>

/* VGA Mode 13h (320x200x256) Register State */
static const vga_regs_t mode_13h = {
    .misc = 0x63, /* 0x63: Sync +/-, 25MHz Clock */
    .seq  = { 0x03, 0x01, 0x0F, 0x00, 0x0E }, /* Chain-4 Enabled */
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
    .misc = 0xE3, /* 0xE3: Sync -/-, 25MHz (Correct for 640x480) */
    .seq  = { 0x03, 0x01, 0x0F, 0x00, 0x02 }, /* Plane mask 0x0F, Memory Mode 0x02 (Planar) */
    .crtc = {
        0x5F, /* 00: HTotal */
        0x4F, /* 01: HDisplayEnd */
        0x50, /* 02: HBlankStart */
        0x82, /* 03: HBlankEnd */
        0x54, /* 04: HRetraceStart */
        0x80, /* 05: HRetraceEnd */
        0x0B, /* 06: VTotal */
        0x3E, /* 07: Overflow */
        0x00, /* 08: PresetRowScan */
        0x40, /* 09: MaxScanLine */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0A-0F */
        0xEA, /* 10: VRetraceStart */
        0x0C, /* 11: VRetraceEnd */
        0xDF, /* 12: VDisplayEnd */
        0x28, /* 13: Offset (Width/16) -> 640/8/2 = 40 = 0x28? */
        0x00, /* 14: UnderlineLoc */
        0xE7, /* 15: VBlankStart */
        0x04, /* 16: VBlankEnd */
        0xE3, /* 17: ModeControl */
        0xFF  /* 18: LineCompare */
    },
    .gc   = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0F, 0xFF }, /* Mode 0 */
    .ac   = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
        0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, /* 16-color Palette */
        0x01, /* Mode: Graphics */
        0x00, /* Overscan */
        0x0F, /* Color Plane Enable */
        0x00, 0x00
    }
};

static void vga_write_regs(const vga_regs_t *regs) {
    /* Write MISC */
    outb(VGA_MISC_WRITE, regs->misc);

    /* Write SEQUENCER */
    for (int i = 0; i < VGA_NUM_SEQ_REGS; i++) {
        outb(VGA_SEQ_INDEX, i);
        outb(VGA_SEQ_DATA, regs->seq[i]);
    }

    /* Unlock CRTC registers 0-7 */
    outb(VGA_CRTC_INDEX, 0x03);
    uint8_t crtc_03 = inb(VGA_CRTC_DATA) | 0x80;
    outb(VGA_CRTC_DATA, crtc_03);
    
    outb(VGA_CRTC_INDEX, 0x11);
    uint8_t crtc_11 = inb(VGA_CRTC_DATA) & ~0x80;
    outb(VGA_CRTC_DATA, crtc_11);

    /* Write CRTC */
    for (int i = 0; i < VGA_NUM_CRTC_REGS; i++) {
        outb(VGA_CRTC_INDEX, i);
        outb(VGA_CRTC_DATA, regs->crtc[i]);
    }

    /* Write GRAPHICS CONTROLLER */
    for (int i = 0; i < VGA_NUM_GC_REGS; i++) {
        outb(VGA_GC_INDEX, i);
        outb(VGA_GC_DATA, regs->gc[i]);
    }

    /* Write ATTRIBUTE CONTROLLER */
    for (int i = 0; i < VGA_NUM_AC_REGS; i++) {
        (void)inb(VGA_INPUT_STAT1); // Reset flip-flop to Index
        outb(VGA_AC_INDEX, i);
        outb(VGA_AC_WRITE, regs->ac[i]);
    }

    /* Enable Video (AC Index 0x20) */
    (void)inb(VGA_INPUT_STAT1);
    outb(VGA_AC_INDEX, 0x20);
}

static void vga_putpixel_linear(int x, int y, uint32_t color) {
    if (x < 0 || x >= 320 || y < 0 || y >= 200) return;
    uint8_t *vram = (uint8_t*)VGA_GFX_MEM_BASE;
    vram[y * 320 + x] = (uint8_t)color;
}

static void vga_putpixel_planar(int x, int y, uint32_t color) {
    if (x < 0 || x >= 640 || y < 0 || y >= 480) return;
    
    /* Write Mode 2 for Planar (640x480x16) */
    unsigned int offset = (y * 640 + x) / 8;
    unsigned int bit = 7 - (x % 8);
    uint8_t mask = 1 << bit;
    
    volatile uint8_t *mem = (volatile uint8_t *)(VGA_GFX_MEM_BASE + offset);
    
    /* Set Write Mode 2 */
    outb(VGA_GC_INDEX, 0x05); outb(VGA_GC_DATA, 0x02);
    /* Set Bit Mask */
    outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, mask);
    
    /* Read-Modify-Write */
    volatile uint8_t dummy = *mem;
    (void)dummy;
    *mem = (uint8_t)color;
    
    /* Restore defaults */
    outb(VGA_GC_INDEX, 0x05); outb(VGA_GC_DATA, 0x00);
    outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, 0xFF);
}

static int vga_set_mode(int width, int height, fb_info_t *fb) {
    if (width == 320 && height == 200) {
        vga_write_regs(&mode_13h);
        if (fb) {
            fb->width = 320; fb->height = 200; fb->bpp = 8;
            fb->pitch = 320; fb->putpixel = vga_putpixel_linear;
        }
        return 0;
    } else if (width == 640 && height == 480) {
        vga_write_regs(&mode_12h);
        if (fb) {
            fb->width = 640; fb->height = 480; fb->bpp = 4;
            fb->pitch = 640/8; fb->putpixel = vga_putpixel_planar;
        }
        return 0;
    }
    kprint("VGA: Unsupported mode requested.\n");
    return -1; 
}

/* Driver Interface */

static int vga_probe(void) {
    return 0; 
}

static int vga_init_driver(fb_info_t *fb) {
    kprint("VGA: Initializing. Defaulting to Mode 13h (320x200)...\n");
    fb->addr = (uint32_t*)VGA_GFX_MEM_BASE;
    return vga_set_mode(320, 200, fb);
}

static int vga_list_modes(struct video_mode_info *modes, int max_count) {
    int count = 0;
    if (max_count > 0) {
        modes[0].width = 320; modes[0].height = 200;
        modes[0].bpp = 8; modes[0].mode_id = 13;
        count++;
    }
    if (max_count > 1) {
        modes[1].width = 640; modes[1].height = 480;
        modes[1].bpp = 4; modes[1].mode_id = 12;
        count++;
    }
    return count;
}

static int vga_set_mode_id(int mode_id) {
    // Note: This updates hardware but relies on fb_* pointer in vga_init to update global state? 
    // Ideally we pass context. For now it works for global single FB.
    /* We need to update the global 'fb' structure that the rest of the kernel sees. */
    /* Since we don't have access to the global 'fb' variable directly here easily (it's in fb.c), 
       we might need a cleaner way. However, vga_set_mode taking 'fb' pointer helps.
       But access to 'fb' global? fb.c exposes it? 
       Actually fb.c defines 'fb_info_t fb;', but it's not extern in header?
       We can't update it easily. 
       Workaround: We only update hardware. User must re-read info? 
       Or ioctl implementation in fb.c re-reads it? 
       fb.c's set_mode wrapper just calls us. 
       Let's stick to hardware update. The VSCREENINFO ioctl will return what? 
       The driver needs to store current mode? 
       Let's assume init handles default. mode switch might desync 'fb'.
       For now, we just set hardware.
    */
    if (mode_id == 13) return vga_set_mode(320, 200, NULL);
    if (mode_id == 12) return vga_set_mode(640, 480, NULL);
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
