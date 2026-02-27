#include <drivers/video/vga.h>
#include <kern/console.h>
#include <arch/x86-common/io.h>
#include <stdint.h>
#include <string.h>

/* Global framebuffer state (from fb.c) */
extern fb_info_t fb;

/* ================== Forward Declarations ================== */
static void vga_putpixel_linear(int x, int y, uint32_t color);
static void vga_putpixel_planar(int x, int y, uint32_t color);
static void herc_putpixel(int x, int y, uint32_t color);
static void cga_putpixel(int x, int y, uint32_t color);

/* ================== Mode Definitions ================== */

/* ================== Mode Definitions ================== */

/* Mode 13h Register Data */
static const vga_regs_t regs_13h = {
    .misc = VGA_MISC_CLK_25MHZ | VGA_MISC_HSYNC_NEG | VGA_MISC_VSYNC_NEG | VGA_MISC_IO_ADDR_SEL | VGA_MISC_RAM_EN,
    .seq  = { 0x03, 0x01, 0x0F, 0x00, VGA_SEQ_MEM_MODE_CHAIN4 | VGA_SEQ_MEM_MODE_EXT_MEM | VGA_SEQ_MEM_MODE_ODD_EVEN },
    .crtc = {
        0x5F, /* 00: HTotal */
        0x4F, /* 01: HDisplay */
        0x50, /* 02: HBlankStart */
        0x82, /* 03: HBlankEnd */
        0x54, /* 04: HRetraceStart */
        0x80, /* 05: HRetraceEnd */
        0xBF, /* 06: VTotal */
        0x1F, /* 07: Overflow */
        0x00, /* 08: PresetRow */
        0x41, /* 09: MaxScan (Double Scan) */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0A-0F */
        0x9C, /* 10: VRetraceStart */
        0x0E, /* 11: VRetraceEnd */
        0x8F, /* 12: VDisplayEnd */
        0x28, /* 13: Offset */
        0x40, /* 14: Underline */
        0x96, /* 15: VBlankStart */
        0xB9, /* 16: VBlankEnd */
        0xA3, /* 17: ModeControl */
        0xFF  /* 18: LineCompare */
    },
    .gc   = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF },
    .ac   = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x41, 0x00, 0x0F, 0x00, 0x00
    }
};

/* Mode 12h Register Data (VGA 640x480x16) */
static const vga_regs_t regs_12h = {
    .misc = VGA_MISC_CLK_25MHZ | VGA_MISC_HSYNC_NEG | VGA_MISC_VSYNC_NEG | VGA_MISC_IO_ADDR_SEL | VGA_MISC_RAM_EN | VGA_MISC_PAGE_ODD_EVEN, /* 0xE3 */
    .seq  = { 0x03, 0x01, 0x0F, 0x00, VGA_SEQ_MEM_MODE_EXT_MEM }, /* 0x02 */
    .crtc = {
        0x5F, /* 00: HTotal */
        0x4F, /* 01: HDisplay */
        0x50, /* 02: HBlankStart */
        0x82, /* 03: HBlankEnd */
        0x54, /* 04: HRetraceStart */
        0x80, /* 05: HRetraceEnd */
        0x0B, /* 06: VTotal (Low) */
        0x3E, /* 07: Overflow (VTotal Bit 8/9, VDisp Bit 8/9, VSync Bit 8) */
        0x00, /* 08: PresetRow */
        0x40, /* 09: MaxScan */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0A-0F */
        0xEA, /* 10: VRetraceStart */
        0x0C, /* 11: VRetraceEnd */
        0xDF, /* 12: VDisplayEnd */
        0x28, /* 13: Offset (Width/16) */
        0x00, /* 14: Underline */
        0xE7, /* 15: VBlankStart */
        0x04, /* 16: VBlankEnd */
        0xE3, /* 17: ModeControl */
        0xFF  /* 18: LineCompare */
    },
    .gc   = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0F, 0xFF },
    .ac   = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
        0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
        0x01, 0x00, 0x0F, 0x00, 0x00
    }
};

/* Mode 10h Register Data (EGA/VGA 640x350x16) */
static const vga_regs_t regs_10h = {
    .misc = VGA_MISC_CLK_28MHZ | VGA_MISC_IO_ADDR_SEL | VGA_MISC_RAM_EN | VGA_MISC_PAGE_ODD_EVEN, /* 0xA7 */
    .seq  = { 0x03, 0x01, 0x0F, 0x00, 0x06 },
    .crtc = { 
        0x5B, /* 00: HTotal */
        0x4F, /* 01: HDisplay */
        0x53, /* 02: HBlankStart */
        0x37, /* 03: HBlankEnd */
        0x52, /* 04: HRetraceStart */
        0x00, /* 05: HRetraceEnd */
        0x6C, /* 06: VTotal */
        0x1F, /* 07: Overflow */
        0x00, /* 08: PresetRow */
        0x00, /* 09: MaxScan */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0A-0F */
        0x5E, /* 10: VRetraceStart */
        0x2B, /* 11: VRetraceEnd */
        0x5D, /* 12: VDisplayEnd */
        0x28, /* 13: Offset */
        0x0F, /* 14: Underline */
        0x96, /* 15: VBlankStart */
        0xB9, /* 16: VBlankEnd */
        0xA3, /* 17: ModeControl */
        0xFF  /* 18: LineCompare */
    },
    .gc   = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF },
    .ac   = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
        0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 
        0x01, 0x00, 0x0F, 0x00, 0x00 
    }
};

/* Custom Init Functions */
static void init_hercules(void) {
    /* Hercules 720x348 Graphics Mode Register Values (Page 0) */
    static const uint8_t regs[] = {
        0x35, /* 00: HTotal (53+1 chars? 720/9=80 chars? Herc uses 90?) */
        0x2D, /* 01: HDisplay (45) */
        0x2E, /* 02: HSyncPos (46) */
        0x07, /* 03: SyncWidth (7) */
        0x5B, /* 04: VTotal (91 rows -> 364 lines) */
        0x02, /* 05: VAdjust (2 scanlines) */
        0x57, /* 06: VDisplay (87 rows -> 348 lines) */
        0x57, /* 07: VSyncPos (87) */
        0x02, /* 08: Interlace (Mode 2?) */
        0x03, /* 09: MaxScan (4 scanlines/row) -> 0x03 = 4 lines? */
        0x00, /* 10: CursorStart */
        0x00  /* 11: CursorEnd */
    };
    
    /* Configuration Switch (0x3BF): Bit 0=Allow Graphics, Bit 1=Page 1 */
    outb(0x3BF, 0x01); 
    
    /* Program 6845 CRT Controller */
    for (int i = 0; i < 12; i++) {
        outb(0x3B4, i);
        outb(0x3B5, regs[i]);
    }
    
    /* Mode Control (0x3B8): 
       Bit 1: Graphics Mode
       Bit 3: Video Enable
       Bit 5: Blink Enable
       Val 0x0A = 0000 1010 -> Graphics | Video Enable
    */
    outb(0x3B8, 0x0A); 
}

static void init_cga(void) {
    /* CGA Mode 4 (320x200x4) Register Values */
    static const uint8_t regs[] = {
        0x38, /* 00: HTotal (56 chars) */
        0x28, /* 01: HDisplay (40 chars -> 320 px) */
        0x2D, /* 02: HSyncPos (45) */
        0x0A, /* 03: SyncWidth (10) */
        0x7F, /* 04: VTotal (127 rows -> 128 typically) */
        0x06, /* 05: VAdjust (6 scanlines) */
        0x64, /* 06: VDisplay (100 rows -> 200 lines) */
        0x70, /* 07: VSyncPos (112) */
        0x02, /* 08: Interlace */
        0x01, /* 09: MaxScan (1+1 = 2 lines/row) */
        0x00, /* 10: CursorStart */
        0x00, /* 11: CursorEnd */
        0x00, /* 12: StartAddrH */
        0x00  /* 13: StartAddrL */
    };

    /* Mode Control (0x3D8): 
       Bit 0: 40/80 Col (0=40)
       Bit 1: Graphics (1)
       Bit 2: Color/BW (0=Color)
       Bit 3: Video Enable (1)
       Bit 4: 320/640 (0=320)
       Bit 5: Blink
       Val 0x0A = 0000 1010 -> Graphics | Video Enable
    */
    outb(0x3D8, 0x0A);
    
    /* Program 6845 CRT Controller */
    for (int i = 0; i < 14; i++) {
        outb(0x3D4, i);
        outb(0x3D5, regs[i]);
    }
    
    /* Color Select (0x3D9): 
       Bit 0-3: Overscan/Background (0)
       Bit 4: Intensity (1)
       Bit 5: Palette (1 = Cyan/Magenta/White)
       Val 0x30 = 0011 0000
    */
    outb(0x3D9, 0x30);
    
    kprint("VGA: CGA Mode 4 initialized.\n");
}

/* Mode Table */
/* Prioritize standard VGA modes */
static const vga_mode_def_t vga_modes[] = {
    { .width = 640, .height = 480, .bpp = 4, .mode_id = 12, .regs = &regs_12h, .init_func = NULL, .putpixel = vga_putpixel_planar, .mem_base = VGA_GFX_MEM_BASE, .pitch = 80 },
    { .width = 320, .height = 200, .bpp = 8, .mode_id = 13, .regs = &regs_13h, .init_func = NULL, .putpixel = vga_putpixel_linear, .mem_base = VGA_GFX_MEM_BASE, .pitch = 320 },
    { .width = 640, .height = 350, .bpp = 4, .mode_id = 10, .regs = &regs_10h, .init_func = NULL, .putpixel = vga_putpixel_planar, .mem_base = VGA_GFX_MEM_BASE, .pitch = 80 },
    /* Legacy / Special Modes */
    { .width = 720, .height = 348, .bpp = 1, .mode_id = 7,  .regs = NULL,      .init_func = init_hercules, .putpixel = herc_putpixel,       .mem_base = VGA_HERC_MEM_BASE, .pitch = 90 },
    { .width = 320, .height = 200, .bpp = 2, .mode_id = 4,  .regs = NULL,      .init_func = init_cga,      .putpixel = cga_putpixel,        .mem_base = VGA_CGA_MEM_BASE,  .pitch = 80 }
};

#define VGA_MODE_COUNT (sizeof(vga_modes) / sizeof(vga_modes[0]))

/* ================== Hardware IO ================== */

static void vga_write_regs(const vga_regs_t *regs) {
    /* Determine CRTC port based on Misc Output Bit 0 (0=B4, 1=D4) */
    uint16_t crtc_index_port = (regs->misc & VGA_MISC_IO_ADDR_SEL) ? VGA_CRTC_INDEX_COLOR : VGA_CRTC_INDEX_MONO;
    uint16_t crtc_data_port  = (regs->misc & VGA_MISC_IO_ADDR_SEL) ? VGA_CRTC_DATA_COLOR : VGA_CRTC_DATA_MONO;
    uint16_t input_stat_port = (regs->misc & VGA_MISC_IO_ADDR_SEL) ? VGA_INPUT_STAT1_COLOR : VGA_INPUT_STAT1_MONO;

    outb(VGA_MISC_WRITE, regs->misc);

    for (int i = 0; i < VGA_NUM_SEQ_REGS; i++) {
        outb(VGA_SEQ_INDEX, i);
        outb(VGA_SEQ_DATA, regs->seq[i]);
    }

    /* Unlock CRTC */
    outb(crtc_index_port, 0x03);
    uint8_t crtc_03 = inb(crtc_data_port) | 0x80;
    outb(crtc_data_port, crtc_03);
    
    outb(crtc_index_port, 0x11);
    uint8_t crtc_11 = inb(crtc_data_port) & ~0x80;
    outb(crtc_data_port, crtc_11);

    for (int i = 0; i < VGA_NUM_CRTC_REGS; i++) {
        outb(crtc_index_port, i);
        outb(crtc_data_port, regs->crtc[i]);
    }

    for (int i = 0; i < VGA_NUM_GC_REGS; i++) {
        outb(VGA_GC_INDEX, i);
        outb(VGA_GC_DATA, regs->gc[i]);
    }

    for (int i = 0; i < VGA_NUM_AC_REGS; i++) {
        (void)inb(input_stat_port); 
        outb(VGA_AC_INDEX, i);
        outb(VGA_AC_WRITE, regs->ac[i]);
    }

    (void)inb(input_stat_port);
    outb(VGA_AC_INDEX, 0x20);
}

/* ================== Drawing Primitives ================== */

static void vga_putpixel_linear(int x, int y, uint32_t color) {
    if (x < 0 || x >= (int)fb.width || y < 0 || y >= (int)fb.height) return;
    volatile uint8_t *vram = (volatile uint8_t *)fb.addr;
    vram[y * fb.pitch + x] = (uint8_t)color;
}

static void vga_putpixel_planar(int x, int y, uint32_t color) {
    if (x < 0 || x >= (int)fb.width || y < 0 || y >= (int)fb.height) return;
    
    /* Planar: 8 pixels per byte */
    unsigned int offset = y * fb.pitch + (x / 8);
    unsigned int bit = 7 - (x % 8);
    uint8_t mask = 1 << bit;
    
    volatile uint8_t *mem = (volatile uint8_t *)((uintptr_t)fb.addr + offset);
    
    /* Set Write Mode 2 */
    outb(VGA_GC_INDEX, VGA_GC_MODE); 
    outb(VGA_GC_DATA, 0x02); 
    
    /* Set Bit Mask */
    outb(VGA_GC_INDEX, VGA_GC_BIT_MASK); 
    outb(VGA_GC_DATA, mask); 
    
    /* Read-Modify-Write to latch */
    volatile uint8_t dummy = *mem; 
    (void)dummy;
    *mem = (uint8_t)color;
    
    /* Restore defaults (Optional, but good practice if mixed 2/0 usage) */
    outb(VGA_GC_INDEX, VGA_GC_MODE); 
    outb(VGA_GC_DATA, 0x00);
    outb(VGA_GC_INDEX, VGA_GC_BIT_MASK); 
    outb(VGA_GC_DATA, 0xFF);
}

static void herc_putpixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= (int)fb.width || y < 0 || y >= (int)fb.height) return;
    
    /* 4-way Interleave */
    /* Bank 0: Lines 0, 4... at Base + 0x0000 */
    /* Bank 1: Lines 1, 5... at Base + 0x2000 */
    /* Bank 2: Lines 2, 6... at Base + 0x4000 */
    /* Bank 3: Lines 3, 7... at Base + 0x6000 */
    
    uintptr_t bank_offset = (y & 3) * 0x2000;
    uintptr_t row_offset = (y >> 2) * fb.pitch;
    
    /* x is in bits */
    uintptr_t byte_offset = x >> 3;
    uint8_t bit_mask = 0x80 >> (x & 7);
    
    volatile uint8_t *mem = (volatile uint8_t *)((uintptr_t)fb.addr + bank_offset + row_offset + byte_offset);
    
    if (color) *mem |= bit_mask;
    else       *mem &= ~bit_mask;
}

static void cga_putpixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= (int)fb.width || y < 0 || y >= (int)fb.height) return;
    
    /* 2-way Interleave */
    /* Even Lines: Base */
    /* Odd Lines: Base + 0x2000 */
    int bank_shift = (y & 1) ? 13 : 0; /* 0x2000 = 1<<13 */
    uintptr_t offset = (y >> 1) * fb.pitch + (x / 4);
    
    volatile uint8_t *mem = (volatile uint8_t *)((uintptr_t)fb.addr + (1 << bank_shift) + offset);
    
    /* 2 bits per pixel */
    int shift = 6 - ((x % 4) * 2);
    uint8_t mask = 0x03 << shift;
    
    *mem = (*mem & ~mask) | ((color & 0x03) << shift);
}

/* ================== Driver Interface ================== */

static int vga_list_modes(struct video_mode_info *modes, int max_count) {
    if (!modes) return VGA_MODE_COUNT;

    int count = 0;
    for (unsigned int i = 0; i < VGA_MODE_COUNT && count < max_count; i++) {
        modes[count].width = vga_modes[i].width;
        modes[count].height = vga_modes[i].height;
        modes[count].bpp = vga_modes[i].bpp;
        modes[count].mode_id = vga_modes[i].mode_id;
        modes[count].flags = 0;
        count++;
    }
    return count;
}

static int vga_set_mode_internal(int mode_id) {
    const vga_mode_def_t *mode = NULL;
    for (unsigned int i = 0; i < VGA_MODE_COUNT; i++) {
        if (vga_modes[i].mode_id == mode_id) {
            mode = &vga_modes[i];
            break;
        }
    }
    
    if (!mode) return -1;
    
    /* Apply Mode */
    if (mode->init_func) {
        mode->init_func();
    } else if (mode->regs) {
        vga_write_regs(mode->regs);
    }
    
    /* Update Global FB State */
    fb.width = mode->width;
    fb.height = mode->height;
    fb.bpp = mode->bpp;
    fb.pitch = mode->pitch;
    fb.addr = (uint32_t*)mode->mem_base;
    fb.putpixel = mode->putpixel;
    
    return 0;
}

static int vga_probe(void) { return 0; }

static int vga_init_driver(fb_info_t *info) {
    kprint("VGA: Initializing Unified Driver [VGA/EGA/CGA/Hercules]\n");
    /* Default Mode 12h (640x480x16) */
    if (vga_set_mode_internal(12) == 0) {
        *info = fb; 
        return 0;
    }
    return -1;
}

static video_driver_t vga_driver = {
    .name = "vga",
    .priority = 20, 
    .probe = vga_probe,
    .init = vga_init_driver,
    .list_modes = vga_list_modes,
    .set_mode = vga_set_mode_internal
};

void vga_install(void) {
    video_register_driver(&vga_driver);
}
