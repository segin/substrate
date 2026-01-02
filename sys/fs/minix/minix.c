#include "minix.h"
#include "../../drivers/video/vga.h"

void minix_init(void) {
    vga_write("Initializing Minix FS Driver...\n", 32);
}

