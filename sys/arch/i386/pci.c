#include "pci.h"
#include "io.h"
#include "vga.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address;
    uint32_t lbus  = (uint32_t)bus;
    uint32_t lslot = (uint32_t)slot;
    uint32_t lfunc = (uint32_t)func;
    uint32_t tmp = 0;
 
    address = (uint32_t)((lbus << 16) | (lslot << 11) |
              (lfunc << 8) | (offset & 0xfc) | ((uint32_t)0x80000000));
 
    outl(PCI_CONFIG_ADDRESS, address);
    tmp = (uint32_t)((inl(PCI_CONFIG_DATA) >> ((offset & 2) * 8)) & 0xffff);
    return tmp;
}

void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t address;
    uint32_t lbus  = (uint32_t)bus;
    uint32_t lslot = (uint32_t)slot;
    uint32_t lfunc = (uint32_t)func;
 
    address = (uint32_t)((lbus << 16) | (lslot << 11) |
              (lfunc << 8) | (offset & 0xfc) | ((uint32_t)0x80000000));
 
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, val);
}

void pci_check_device(uint8_t bus, uint8_t device) {
    uint8_t function = 0;
 
    uint32_t vendorID = pci_read(bus, device, function, 0);
    if(vendorID == 0xFFFF) return; // Device doesn't exist

    // If device exists, we can print it
    vga_write("PCI Device found.\n", 18);
    // Real implementation would check header type and scan functions
}

void pci_scan(void) {
    for(uint16_t bus = 0; bus < 256; bus++) {
        for(uint8_t device = 0; device < 32; device++) {
            pci_check_device(bus, device);
        }
    }
}

void pci_init(void) {
    vga_write("Scanning PCI Bus...\n", 20);
    pci_scan();
}
