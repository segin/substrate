#include "ahci.h"
#include "../../video/vga.h"
#include "../../../arch/i386/pci.h"

void ahci_init(void) {
    vga_write("AHCI Driver Initialized.\n", 25);
    // Would scan PCI for Class 0x01 Subclass 0x06
}

