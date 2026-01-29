#include <kern/console.h>
#include <drivers/storage/ahci/ahci.h>
#include <arch/i386/pci.h>

void ahci_init(void) {
    kprint("AHCI Driver Initialized.\n");
    // Would scan PCI for Class 0x01 Subclass 0x06
}

