#include "../../arch/x86-common/include/io.h"
#include <stdint.h>
#include <stddef.h>

#define EGA_MEM_BASE    0xA0000
#define EGA_CTRL_PORT   0x3C4

void ega_init(void) {
    // EGA Planar mode initialization
}

void ega_putpixel(int x, int y, uint8_t color) {
    // EGA uses 4 color planes
    // Select planes and write to memory at 0xA0000
    (void)x; (void)y; (void)color;
}
