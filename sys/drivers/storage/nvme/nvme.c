#include "nvme.h"
#include "../../video/vga.h"
#include "../../../arch/i386/pci.h"

void nvme_init(void) {
    vga_write("NVMe Driver Initialized.\n", 25);
    // Would scan PCI for Class 0x01 Subclass 0x08
}

