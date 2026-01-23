#include <drivers/storage/nvme.h>
#include <kern/console.h>
#include <arch/i386/pci.h>

void nvme_init(void) {
    kprint("NVMe Driver Initialized.\n");
    // Would scan PCI for Class 0x01 Subclass 0x08
}

