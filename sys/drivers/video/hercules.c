#include <drivers/video/hercules.h>
#include <arch/x86-common/include/io.h>
#include <stdint.h>
#include <stddef.h> /* For NULL if needed, though we use explicit addresses */

static uint8_t *herc_mem = (uint8_t *)HERC_MEM_BASE;

static void herc_putpixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= 720 || y < 0 || y >= 348) return;

    /*
     * Hercules standard: 720x348, Mono
     * Interleaved in 4 banks:
     * Line 0: Base + 0
     * Line 1: Base + 0x2000
     * Line 2: Base + 0x4000
     * Line 3: Base + 0x6000
     */
    
    uintptr_t row_offset = (y >> 2) * 90;
    uintptr_t bank_offset = (y & 3) * 0x2000;
    uintptr_t global_offset = bank_offset + row_offset + (x >> 3);
    
    uint8_t bit_mask = 0x80 >> (x & 7);

    if (color) {
        herc_mem[global_offset] |= bit_mask;
    } else {
        herc_mem[global_offset] &= ~bit_mask;
    }
}

int herc_init(fb_info_t *fb) {
    /* 720x348 Graphics Mode Register Values */
    static const uint8_t regs[] = {
        0x35, 0x2D, 0x2E, 0x07, 0x5B, 0x02, 0x57, 0x57, 0x02, 0x03, 0x00, 0x00
    };

    /* Enable Graphics Paging (allow 64K access for both pages if needed) */
    outb(HERC_CONFIG_SWITCH, 0x01); // 0x01 = Allow graphics mode, 0x00 = No graphics

    /* Program CRT Controller */
    for (int i = 0; i < 12; i++) {
        outb(HERC_INDEX_PORT, i);
        outb(HERC_DATA_PORT, regs[i]);
    }

    /* Enable Video + Graphics Mode */
    /* 0x0A = Video Enable (0x08) | Graphics Mode (0x02) */
    outb(HERC_MODE_CONTROL, HERC_MODE_VIDEO_EN | HERC_MODE_GRAPHICS);
    
    /* Fill fb info */
    fb->addr = (uint32_t*)HERC_MEM_BASE;
    fb->width = 720;
    fb->height = 348;
    fb->pitch = 90; // 720 bits / 8 = 90 bytes
    fb->bpp = 1;
    fb->putpixel = herc_putpixel;
    
    return 0;
}

/* Hercules Probe (Simple check if register ports are readable or blindly ok for now) */
static int herc_probe(void) {
    /* TODO: Better detection. Ideally check BIOS equipment word or port response */
    /* For now, assume if user asks for it, it's there? */
    /* Or only if not competing with CGA/VGA? */
    /* We return 0 so it CAN be selected via cmdline. Priority low so won't be default. */
    return 0;
}

static video_driver_t herc_driver = {
    .name = "hercules",
    .priority = 5,
    .probe = herc_probe,
    .init = herc_init
};

void herc_install(void) {
    video_register_driver(&herc_driver);
}
