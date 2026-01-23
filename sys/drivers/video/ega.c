#include <drivers/video/ega.h>
#include <kern/console.h>
#include <arch/x86-common/include/io.h>
#include <stdint.h>
#include <stddef.h>

// Standard EGA register values for 640x350 16-color (Mode 0x10)
static uint8_t ega_seq_regs[] = { 0x03, 0x01, 0x0F, 0x00, 0x06 }; 
static uint8_t ega_crtc_regs[] = { 
    0x5B, 0x4F, 0x53, 0x37, 0x52, 0x00, 0x6C, 0x1F,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x5E, 0x2B, 0x5D, 0x28, 0x0F, 0x96, 0xB9, 0xA3, 
    0xFF 
};
static uint8_t ega_gc_regs[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF };
static uint8_t ega_ac_regs[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 
    0x01, 0x00, 0x0F, 0x00 
};

static void ega_write_regs(void) {
    // Sequencer
    outb(EGA_SEQ_INDEX, EGA_SEQ_RESET); outb(EGA_SEQ_DATA, 0x01); // Async Reset
    for (int i = 0; i < (int)sizeof(ega_seq_regs); i++) {
        outb(EGA_SEQ_INDEX, i);
        outb(EGA_SEQ_DATA, ega_seq_regs[i]);
    }
    outb(EGA_SEQ_INDEX, EGA_SEQ_RESET); outb(EGA_SEQ_DATA, 0x03); // Reset complete

    // Misc Output
    outb(EGA_MISC_OUT_W, 0xA7); // 28MHz, Enable RAM, Sync +/-, Page 0

    // CRTC
    for (int i = 0; i < (int)sizeof(ega_crtc_regs); i++) {
        outb(EGA_CRTC_INDEX, i);
        outb(EGA_CRTC_DATA, ega_crtc_regs[i]);
    }

    // Graphics Controller
    for (int i = 0; i < (int)sizeof(ega_gc_regs); i++) {
        outb(EGA_GC_INDEX, i);
        outb(EGA_GC_DATA, ega_gc_regs[i]);
    }

    // Attribute Controller
    for (int i = 0; i < (int)sizeof(ega_ac_regs); i++) {
        (void)inb(EGA_INPUT_STAT1); // Reset flip-flop
        outb(EGA_AC_INDEX, i);
        outb(EGA_AC_WRITE, ega_ac_regs[i]);
    }
    (void)inb(EGA_INPUT_STAT1);
    outb(EGA_AC_INDEX, 0x20); // Enable video
}

void ega_init(void) {
    kprint("EGA: Initializing Hardware (640x350 Planar)...\n");
    ega_write_regs();
    
    // Verify state: Check VRetrace in Status Register 1
    // Simplistic verification: Wait for retrace toggles
    int changes = 0;
    uint8_t init_val = inb(EGA_INPUT_STAT1) & 0x08;
    for (int i = 0; i < 10000; i++) {
        if ((inb(EGA_INPUT_STAT1) & 0x08) != init_val) {
            changes++;
            init_val = inb(EGA_INPUT_STAT1) & 0x08;
        }
    }
    
    if (changes > 0) {
        kprint("EGA: Hardware state verified (VRetrace active).\n");
    } else {
        kprint("EGA: Warning - Hardware state verification failed (No VRetrace).\n");
    }
    
    // Clear screen
    // Map Mask to all planes
    outb(EGA_SEQ_INDEX, EGA_SEQ_MAP_MASK); outb(EGA_SEQ_DATA, 0x0F);
    // Write 0 to standard memory
    uint8_t *vram = (uint8_t *)EGA_MEM_BASE;
    for (int i = 0; i < 32000; i++) vram[i] = 0; // Approx clear
}

void ega_putpixel(int x, int y, uint8_t color) {
    // EGA uses 4 color planes
    // Select planes and write to memory at 0xA0000
    (void)x; (void)y; (void)color;
}
