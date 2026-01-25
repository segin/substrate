#include <drivers/video/vga.h>
#include <kern/console.h>
#include <arch/x86-common/include/io.h>
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

/* Mode 12h Register Data */
static const vga_regs_t regs_12h = {
    .misc = VGA_MISC_CLK_25MHZ | VGA_MISC_HSYNC_NEG | VGA_MISC_VSYNC_NEG | VGA_MISC_IO_ADDR_SEL | VGA_MISC_RAM_EN | VGA_MISC_PAGE_ODD_EVEN,
    .seq  = { 0x03, 0x01, 0x0F, 0x00, VGA_SEQ_MEM_MODE_EXT_MEM },
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

/* Mode 10h Register Data (EGA 640x350) */
static const vga_regs_t regs_10h = {
    .misc = VGA_MISC_CLK_28MHZ | VGA_MISC_IO_ADDR_SEL | VGA_MISC_RAM_EN | VGA_MISC_PAGE_ODD_EVEN,
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

/* Custom Init Functions */
static void init_hercules(void) {
    /* Hercules 720x348 Graphics Mode Register Values */
    static const uint8_t regs[] = {
        0x35, 0x2D, 0x2E, 0x07, 0x5B, 0x02, 0x57, 0x57, 0x02, 0x03, 0x00, 0x00
    };
    outb(0x3BF, 0x01); /* Config Switch: Allow Graphics */
    
    /* Program 6845 CRT Controller */
    for (int i = 0; i < 12; i++) {
        outb(0x3B4, i);
        outb(0x3B5, regs[i]);
    }
    
    outb(0x3B8, 0x0A); /* Mode Control: Video Enable | Graphics Mode */
}

static void init_cga(void) {
    /* CGA Init Stub: We don't program registers yet, assuming BIOS or pre-set */
    /* Ideally we'd set 320x200x4 (Mode 4) or 640x200x2 (Mode 6) regs */
    kprint("VGA: CGA initialized (stub).\n");
}

/* Mode Table */
/* Prioritize standard VGA modes */
static const vga_mode_def_t vga_modes[] = {
    { .width = 640, .height = 480, .bpp = 4, .mode_id = 12, .regs = &regs_12h, .init_func = NULL, .putpixel = vga_putpixel_planar, .mem_base = VGA_GFX_MEM_BASE, .pitch = 80 },
    { .width = 320, .height = 200, .bpp = 8, .mode_id = 13, .regs = &regs_13h, .init_func = NULL, .putpixel = vga_putpixel_linear, .mem_base = VGA_GFX_MEM_BASE, .pitch = 320 },
    { .width = 640, .height = 350, .bpp = 4, .mode_id = 10, .regs = &regs_10h, .init_func = NULL, .putpixel = vga_putpixel_planar, .mem_base = VGA_GFX_MEM_BASE, .pitch = 80 },
    /* Legacy / Special Modes */
    { .width = 720, .height = 348, .bpp = 1, .mode_id = 7,  .regs = NULL,      .init_func = init_hercules, .putpixel = herc_putpixel,       .mem_base = VGA_HERC_MEM_BASE, .pitch = 90 },
    { .width = 320, .height = 200, .bpp = 2, .mode_id = 6,  .regs = NULL,      .init_func = init_cga,      .putpixel = cga_putpixel,        .mem_base = VGA_CGA_MEM_BASE,  .pitch = 80 }
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
    kprint("VGA: Initializing Unified Driver [VGA/EGA/CGA/MDA]\n");
    /* Default Mode 13h */
    if (vga_set_mode_internal(13) == 0) {
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
