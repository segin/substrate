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

    uint32_t deviceID = pci_read(bus, device, function, 2);
    uint32_t classCode = pci_read(bus, device, function, 0x0A);
    
    // Print device info: bus:dev.func vendor:device class
    static char hex[] = "0123456789ABCDEF";
    char buf[48];
    int i = 0;
    
    buf[i++] = 'P'; buf[i++] = 'C'; buf[i++] = 'I'; buf[i++] = ' ';
    buf[i++] = hex[(bus >> 4) & 0xF]; buf[i++] = hex[bus & 0xF];
    buf[i++] = ':';
    buf[i++] = hex[(device >> 4) & 0xF]; buf[i++] = hex[device & 0xF];
    buf[i++] = '.'; buf[i++] = '0'; buf[i++] = ' ';
    buf[i++] = hex[(vendorID >> 12) & 0xF]; buf[i++] = hex[(vendorID >> 8) & 0xF];
    buf[i++] = hex[(vendorID >> 4) & 0xF]; buf[i++] = hex[vendorID & 0xF];
    buf[i++] = ':';
    buf[i++] = hex[(deviceID >> 12) & 0xF]; buf[i++] = hex[(deviceID >> 8) & 0xF];
    buf[i++] = hex[(deviceID >> 4) & 0xF]; buf[i++] = hex[deviceID & 0xF];
    buf[i++] = ' '; buf[i++] = '[';
    buf[i++] = hex[(classCode >> 12) & 0xF]; buf[i++] = hex[(classCode >> 8) & 0xF];
    buf[i++] = hex[(classCode >> 4) & 0xF]; buf[i++] = hex[classCode & 0xF];
    buf[i++] = ']'; buf[i++] = '\n'; buf[i] = 0;
    
    vga_write(buf, i);
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
