#include <kern/console.h>
#include <drivers/video/vga.h>
#include <arch/x86-common/include/io.h>
#include <stdint.h>
#include <string.h>

// VGA Mode 13h Memory
static uint8_t* const VGA_GRAPHICS_MEMORY = (uint8_t*) 0xA0000;
static const int VGA_GFX_WIDTH = 320;
static const int VGA_GFX_HEIGHT = 200;

// Standard VGA Registers
#define VGA_MISC_WRITE      0x3C2
#define VGA_CRTC_INDEX      0x3D4
#define VGA_CRTC_DATA       0x3D5
#define VGA_SEQ_INDEX       0x3C4
#define VGA_SEQ_DATA        0x3C5
#define VGA_GC_INDEX        0x3CE
#define VGA_GC_DATA         0x3CF
#define VGA_AC_INDEX        0x3C0
#define VGA_AC_WRITE        0x3C0
#define VGA_AC_READ         0x3C1

void vga_write_regs(unsigned char *regs) {
    unsigned int i;

    // write MISCELLANEOUS reg
    outb(VGA_MISC_WRITE, *regs);
    regs++;

    // write SEQUENCER regs
    for (i = 0; i < 5; i++) {
        outb(VGA_SEQ_INDEX, i);
        outb(VGA_SEQ_DATA, *regs);
        regs++;
    }

    // unlock CRTC registers
    outb(VGA_CRTC_INDEX, 0x03);
    outb(VGA_CRTC_DATA, inb(VGA_CRTC_DATA) | 0x80);
    outb(VGA_CRTC_INDEX, 0x11);
    outb(VGA_CRTC_DATA, inb(VGA_CRTC_DATA) & ~0x80);

    // make sure they remain unlocked
    regs[0x03] |= 0x80;
    regs[0x11] &= ~0x80;

    // write CRTC regs
    for (i = 0; i < 25; i++) {
        outb(VGA_CRTC_INDEX, i);
        outb(VGA_CRTC_DATA, *regs);
        regs++;
    }

    // write GRAPHICS CONTROLLER regs
    for (i = 0; i < 9; i++) {
        outb(VGA_GC_INDEX, i);
        outb(VGA_GC_DATA, *regs);
        regs++;
    }

    // write ATTRIBUTE CONTROLLER regs
    for (i = 0; i < 21; i++) {
        (void)inb(0x3DA);
        outb(VGA_AC_INDEX, i);
        outb(VGA_AC_WRITE, *regs);
        regs++;
    }

    (void)inb(0x3DA);
    outb(VGA_AC_INDEX, 0x20);
}

// Mode 13h (320x200x256) Register Values
unsigned char g_320x200x256[] = {
    // MISC
    0x63,
    // SEQ
    0x03, 0x01, 0x0F, 0x00, 0x0E,
    // CRTC
    0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,
    0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x9C, 0x0E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3,
    0xFF,
    // GC
    0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F,
    0xFF,
    // AC
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x41, 0x00, 0x0F, 0x00, 0x00
};

void vga_set_mode(int width, int height) {
    if (width == 320 && height == 200) {
        vga_write_regs(g_320x200x256);
        return;
    }
    // Fallback or text mode requested?
    // If text mode (80x25) is requested, we should probably reset to text mode values.
    // For now, this function focuses on switching TO graphics.
}

void vga_putpixel(int x, int y, uint8_t color) {
    if (x < 0 || x >= VGA_GFX_WIDTH || y < 0 || y >= VGA_GFX_HEIGHT)
        return;
        
    unsigned int offset = VGA_GFX_WIDTH * y + x;
    VGA_GRAPHICS_MEMORY[offset] = color;
}

void vga_init(void) {
    kprint("VGA: Logic initialized (Graphics-ready). Text mode handled by vga_text.\n");
    // We do NOT switch mode here automatically. 
    // It should be driven by kernel cmdline or user request.
}
