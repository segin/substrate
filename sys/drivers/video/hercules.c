#include <arch/x86-common/include/io.h>
#include <stdint.h>
#include <stddef.h>

#define HERC_MEM_BASE   0xB0000
#define HERC_CTRL_PORT  0x3B4

static uint8_t *herc_mem = (uint8_t *)HERC_MEM_BASE;

void herc_init(void) {
    // Initialization for 720x348 graphics mode
}

void herc_putpixel(int x, int y, int color) {
    if (x < 0 || x >= 720 || y < 0 || y >= 348) return;

    // Hercules uses an interleaved memory layout (4 banks)
    uintptr_t addr = (y >> 2) * 90 + (x >> 3);
    uintptr_t bank_offset = (y & 3) * 0x2000;
    uint8_t bit_mask = 0x80 >> (x & 7);

    if (color) herc_mem[bank_offset + addr] |= bit_mask;
    else herc_mem[bank_offset + addr] &= ~bit_mask;
}
