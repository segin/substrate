#include "ext2.h"
#include "../../drivers/video/vga.h" // Temporary include for debug printing

void ext2_init(void) {
    vga_write("Initializing EXT2 Driver...\n", 26);
    // Real implementation would register itself with VFS or probe a device
}

// TODO: Implement read, write, open, close, readdir, finddir for EXT2

