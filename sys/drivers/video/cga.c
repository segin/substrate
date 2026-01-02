#include "../../arch/i386/io.h"
#include <stdint.h>
#include <stddef.h>

#define CGA_MEM_BASE    0xB8000
#define CGA_CTRL_PORT   0x3D4
#define CGA_DATA_PORT   0x3D5

static uint16_t *cga_mem = (uint16_t *)CGA_MEM_BASE;
static int cga_x = 0;
static int cga_y = 0;

void cga_init(void) {
    cga_x = 0;
    cga_y = 0;
}

void cga_putc(char c, uint8_t color) {
    if (c == '\n') {
        cga_x = 0;
        cga_y++;
    } else {
        const int index = cga_y * 80 + cga_x;
        cga_mem[index] = (uint16_t)c | (uint16_t)color << 8;
        cga_x++;
    }

    if (cga_x >= 80) {
        cga_x = 0;
        cga_y++;
    }
    // Scrolling logic would go here
}

