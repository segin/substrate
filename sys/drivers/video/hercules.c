#include <drivers/video/hercules.h>
#include <arch/x86-common/include/io.h>

#define HERC_MEM_BASE   0xB0000
#define HERC_CTRL_PORT  0x3B4

static uint8_t *herc_mem = (uint8_t *)HERC_MEM_BASE;

static void herc_putpixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= 720 || y < 0 || y >= 348) return;

    // Hercules standard: 720x348, Mono
    // 0xB0000 base
    // Interleaved in 4 banks:
    // Line 0: Base + 0
    // Line 1: Base + 0x2000
    // Line 2: Base + 0x4000
    // Line 3: Base + 0x6000
    // Line 4: Base + 90
    
    uintptr_t row_offset = (y >> 2) * 90;
    uintptr_t bank_offset = (y & 3) * 0x2000;
    uintptr_t global_offset = bank_offset + row_offset + (x >> 3);
    
    uint8_t bit_mask = 0x80 >> (x & 7);

    // Simple threshold for color: non-black = on
    if ((color & 0xFFFFFF) != 0) {
        herc_mem[global_offset] |= bit_mask;
    } else {
        herc_mem[global_offset] &= ~bit_mask;
    }
}

int herc_init(fb_info_t *fb) {
    // Basic HGC initialization sequences should go here
    // For now, assuming card is already in text/graphics mode or generic init
    // TODO: Write HGC enable sequence to ports
    
    // Configure CRT Controller
    outb(0x3BF, 0x01); // Enable graphics paging
    outb(0x3B8, 0x0A); // Enable video, high res mode (graphics)
    
    // Fill fb info
    fb->addr = (uint32_t*)HERC_MEM_BASE;
    fb->width = 720;
    fb->height = 348;
    fb->pitch = 90; // 720 bits / 8 = 90 bytes
    fb->bpp = 1;
    fb->putpixel = herc_putpixel;
    
    return 0;
}
