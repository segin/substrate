#include "exfat.h"
#include "../../drivers/video/vga.h"

void exfat_init(void) {
    vga_write("Initializing exFAT Driver...\n", 27);
}

