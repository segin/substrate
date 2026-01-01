#include "ide.h"
#include "../../video/vga.h"
#include "../../../arch/i386/io.h"

#define ATA_PRIMARY_IO   0x1F0
#define ATA_SECONDARY_IO 0x170

void ide_init(void) {
    vga_write("IDE Driver Initialized.\n", 24);
    
    // Simple check for floating bus
    uint8_t status = inb(ATA_PRIMARY_IO + 7);
    if (status == 0xFF) {
        vga_write("IDE: Primary bus floating.\n", 25);
    } else {
        vga_write("IDE: Primary bus present.\n", 24);
    }
}

