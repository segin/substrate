#include <drivers/video/vga.h>
#include <kern/console.h>
#include <arch/x86-common/include/io.h>
#include <stdint.h>
#include <string.h>

/* VGA Mode 13h (320x200x256) Register State */
static const vga_regs_t mode_13h = {
    .misc = 0x63,
    .seq  = { 0x03, 0x01, 0x0F, 0x00, 0x0E },
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

static void vga_putpixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= 320 || y < 0 || y >= 200) return;
    
    // Mode 13h is linear
    uint8_t *vram = (uint8_t*)VGA_GFX_MEM_BASE;
    vram[y * 320 + x] = (uint8_t)color;
}

static int vga_set_mode(int width, int height) {
    if (width == 320 && height == 200) {
        vga_write_regs(&mode_13h);
        return 0;
    }
    kprint("VGA: Unsupported mode requested.\n");
    return -1; 
}

/* Driver Interface */

static int vga_probe(void) {
    /* TODO: Check functionality. Assume standard VGA present for now. */
    /* Could check for VGA ports presence or BIOS signature if needed. */
    return 0; 
}

static int vga_init_driver(fb_info_t *fb) {
    kprint("VGA: Setting Mode 13h (320x200x256)...\n");
    if (vga_set_mode(320, 200) != 0) return -1;
    
    fb->addr = (uint32_t*)VGA_GFX_MEM_BASE;
    fb->width = 320;
    fb->height = 200;
    fb->pitch = 320;
    fb->bpp = 8;
    fb->putpixel = vga_putpixel;
    
    return 0;
}

static int vga_list_modes(struct video_mode_info *modes, int max_count) {
    if (max_count < 1) return 0;
    
    modes[0].width = 320;
    modes[0].height = 200;
    modes[0].bpp = 8;
    modes[0].mode_id = 1;
    modes[0].flags = 0;
    
    return 1;
}

static int vga_set_mode_id(int mode_id) {
    if (mode_id == 1) {
        return vga_set_mode(320, 200);
    }
    return -1;
}

static video_driver_t vga_driver = {
    .name = "vga",
    .priority = 20, /* Higher than CGA/Hercules, lower than BGA */
    .probe = vga_probe,
    .init = vga_init_driver,
    .list_modes = vga_list_modes,
    .set_mode = vga_set_mode_id
};

void vga_install(void) {
    video_register_driver(&vga_driver);
}
