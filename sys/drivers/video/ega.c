#include <drivers/video/ega.h>
#include <kern/console.h>
#include <arch/x86-common/include/io.h>
#include <stdint.h>
#include <stddef.h>

// Standard EGA register values for 640x350 16-color (Mode 0x10)
// Seq: Reset, Clock Mode, Mask, Char Map, Mem Mode
static uint8_t ega_seq_regs[] = { 0x03, 0x01, 0x0F, 0x00, 0x06 }; 

// CRTC: HTotal, HDisplay, HBlank, ...
static uint8_t ega_crtc_regs[] = { 
    0x5B, 0x4F, 0x53, 0x37, 0x52, 0x00, 0x6C, 0x1F,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x5E, 0x2B, 0x5D, 0x28, 0x0F, 0x96, 0xB9, 0xA3, 
    0xFF 
};

// GC: Set/Reset, En, Color Cmp, Rotate, Read Map, Mode, Misc, Color care, Bit Mask
static uint8_t ega_gc_regs[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF };

// AC: Palette 0-15, Mode, Overscan, Color Plane En, HPan
static uint8_t ega_ac_regs[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 
    0x01, 0x00, 0x0F, 0x00 
};

// ... [Skipping to putpixel] ...

void ega_putpixel(int x, int y, uint8_t color) {
    if (x < 0 || x >= 640 || y < 0 || y >= 350) return;

    /* EGA Write Mode 2:
     * 1. Set Write Mode 2 in GC Mode Register (Index 5).
     * 2. Set Bit Mask (Index 8) to select the pixel within the byte.
     * 3. Read the byte (to load latches).
     * 4. Write color index as data (GC expands it to planes).
     */
     
    unsigned int offset = (y * 640 + x) / 8;
    unsigned int bit_index = 7 - (x % 8);
    uint8_t mask = 1 << bit_index;
    
    volatile uint8_t *mem = (volatile uint8_t *)(EGA_MEM_BASE + offset);
    
    /* Set Write Mode 2 (Bits 0-1 = 10) */
    outb(EGA_GC_INDEX, 0x05);
    outb(EGA_GC_DATA, 0x02);
    
    /* Set Bit Mask */
    outb(EGA_GC_INDEX, 0x08);
    outb(EGA_GC_DATA, mask);
    
    /* Read-Modify-Write */
    volatile uint8_t dummy = *mem;
    (void)dummy;
    *mem = color; // Content of write is the color index in Mode 2
    
    /* Restore defaults (Write Mode 0, Bit Mask 0xFF) for other ops? */
    /* Only if expecting mixed usage. For now, we leave it or reset per op.
       Optimization: Don't verify every time if single threaded. */
    outb(EGA_GC_INDEX, 0x05);
    outb(EGA_GC_DATA, 0x00);
    outb(EGA_GC_INDEX, 0x08); 
    outb(EGA_GC_DATA, 0xFF);
}

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

