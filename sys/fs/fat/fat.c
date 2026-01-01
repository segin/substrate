#include "fat.h"
#include "../../drivers/video/vga.h"

void fat_init(void) {
    vga_write("Initializing FAT Driver...\n", 25);
}

