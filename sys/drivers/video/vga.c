#include <drivers/video/vga.h>
#include <kern/console.h>
#include <arch/x86-common/include/io.h>
#include <stdint.h>
#include <string.h>

/* ================== Forward Declarations ================== */
static void vga_putpixel_linear(int x, int y, uint32_t color);
static void vga_putpixel_planar(int x, int y, uint32_t color);
static void herc_putpixel(int x, int y, uint32_t color);
static void cga_putpixel(int x, int y, uint32_t color);

/* ================== Mode Definitions ================== */

typedef struct {
    int width;
    int height;
    int bpp;
    int mode_id;
    const vga_regs_t *regs; /* If NULL, special handling */
    void (*putpixel)(int, int, uint32_t);
    uint32_t mem_base;
    uint32_t pitch;
} vga_mode_def_t;

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

/* Mode Table */
/* NULL regs implies special handling */
static const vga_mode_def_t vga_modes[] = {
    { .width = 320, .height = 200, .bpp = 8, .mode_id = 13, .regs = &regs_13h, .putpixel = vga_putpixel_linear, .mem_base = VGA_GFX_MEM_BASE, .pitch = 320 },
    { .width = 640, .height = 480, .bpp = 4, .mode_id = 12, .regs = &regs_12h, .putpixel = vga_putpixel_planar, .mem_base = VGA_GFX_MEM_BASE, .pitch = 80 },
    { .width = 640, .height = 350, .bpp = 4, .mode_id = 10, .regs = &regs_10h, .putpixel = vga_putpixel_planar, .mem_base = VGA_GFX_MEM_BASE, .pitch = 80 },
    /* Legacy / Special Modes */
    { .width = 720, .height = 348, .bpp = 1, .mode_id = 7,  .regs = NULL,      .putpixel = herc_putpixel,       .mem_base = VGA_HERC_MEM_BASE, .pitch = 90 },
    { .width = 320, .height = 200, .bpp = 2, .mode_id = 6,  .regs = NULL,      .putpixel = cga_putpixel,        .mem_base = VGA_CGA_MEM_BASE,  .pitch = 80 }
};

#define VGA_MODE_COUNT (sizeof(vga_modes) / sizeof(vga_modes[0]))

/* ================== Hardware IO ================== */

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
    /* Assumes 640 width logic for offset calculation */
    /* Ideally should use global width dependent offset */
    /* But standard modes (12h, 10h) use 640 */
    
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

static int vga_set_mode_id(int mode_id) {
    /* Find mode */
    const vga_mode_def_t *mode = NULL;
    for (unsigned int i = 0; i < VGA_MODE_COUNT; i++) {
        if (vga_modes[i].mode_id == mode_id) {
            mode = &vga_modes[i];
            break;
        }
    }
    
    if (!mode) return -1;
    
    /* Hardcoded Special Handling for non-VGA regs support (Hercules/CGA stub) */
    if (mode_id == 7) {
        /* Hercules Init */
        static const uint8_t herc_regs[] = {
            0x35, 0x2D, 0x2E, 0x07, 0x5B, 0x02, 0x57, 0x57, 0x02, 0x03, 0x00, 0x00
        };
        outb(0x3BF, 0x01); // Config
        for(int i=0;i<12;i++) { outb(0x3B4, i); outb(0x3B5, herc_regs[i]); }
        outb(0x3B8, 0x0A); // Mode Control
    } else if (mode_id == 6) {
        kprint("VGA: Setting CGA Mode (Stub). Registers not set.\n");
    } else {
        /* Standard VGA/EGA */
        if (mode->regs) {
            vga_write_regs(mode->regs);
        }
    }
    
    /* Update implementation info (Global 'fb' update logic needed, but here we can only return success?
       The fb_init caller will notice success. But fb pointers?
       We need to update the fb_info passed during init? Or a global driver state?
       The upper layer (fb.c) doesn't query us for fb params after set_mode.
       It assumes we set them?
       Wait, fb.c:206 just checks return.
       We MUST have a way to update the pixel pointer and geometry in the top layer.
       Current design flaw: set_mode doesn't take fb_info* or return it.
       Workaround: fb.c relies on list_modes to know geometry, but pixel pointer?
       We can export a 'vga_get_current_info' or similar?
       Or fb.c can re-call init or probe?
       Actually, `vga_init` sets it initially.
       If we switch mode, `fb.putpixel` might need changing.
       fb.c uses `current_driver` and `fb` global.
       Since `vga.c` knows `vga_putpixel_...`, we need to hook it into `fb`.
       But we don't have access to `fb` here (except in init).
       We can add `vga_get_fb_info`?
       For now, I will add a hack: `extern fb_info_t fb;` (It is non-static in fb.c).
       This is ugly but works for now.
    */
    /* Extern hack to update global state */
    /* Ideally refactor driver API to include context */
    
    return 0;
}

/* Hack to update global fb state on mode switch. */
/* See common/fb.c */
extern fb_info_t fb;

static int vga_set_mode_internal(int mode_id) {
    if (vga_set_mode_id(mode_id) != 0) return -1;
    
    /* Update parameters */
    for (unsigned int i = 0; i < VGA_MODE_COUNT; i++) {
        if (vga_modes[i].mode_id == mode_id) {
            fb.width = vga_modes[i].width;
            fb.height = vga_modes[i].height;
            fb.bpp = vga_modes[i].bpp;
            fb.pitch = vga_modes[i].pitch;
            fb.addr = (uint32_t*)vga_modes[i].mem_base;
            fb.putpixel = vga_modes[i].putpixel;
            return 0;
        }
    }
    return -1;
}

static int vga_probe(void) { return 0; }

static int vga_init_driver(fb_info_t *info) {
    kprint("VGA: Initializing Unified Driver [VGA/EGA/CGA/MDA]\n");
    /* Default Mode 13h */
    if (vga_set_mode_internal(13) == 0) {
        /* Sync info */
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
