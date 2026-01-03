#include "virtio.h"
#include "../../arch/i386/pci.h"
#include "../../arch/i386/io.h"
#include <kern/console.h>
#include <stdio.h>

// Re-implement basic PCI read/write here or expose them from arch pci.c
// But pci.c uses internal helpers. Let's assume we can link against pci.o.
// However pci.c didn't expose read/write in pci.h?
// Let's check pci.h. If not, I'll copy the logic (IO ports 0xCF8/0xCFC).

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

static uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xfc) | 0x80000000);
    outl(PCI_CONFIG_ADDRESS, address);
    uint32_t tmp = inl(PCI_CONFIG_DATA);
    return (tmp >> ((offset & 2) * 8)) & 0xffff; // This reads 16-bit? Logic in pci.c was complex.
    // Simplifying: Just read 32-bit and shift/mask if needed.
    // Actually, let's just stick to 32-bit reads for simplest access.
}

static uint32_t pci_read_32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xfc) | 0x80000000);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

// Find capabilities / BARs
uint16_t virtio_get_io_base(uint8_t bus, uint8_t slot, uint8_t func) {
    // Read BAR0 (Offset 0x10)
    uint32_t bar0 = pci_read_32(bus, slot, func, 0x10);
    if (bar0 & 1) { // IO Space
        return (uint16_t)(bar0 & 0xFFFC);
    }
    return 0;
}

void virtio_init(void) {
    // Scan for VirtIO devices
    for(uint16_t bus = 0; bus < 256; bus++) {
        for(uint8_t device = 0; device < 32; device++) {
            uint32_t vid = pci_read_config(bus, device, 0, 0); // Vendor ID
            if (vid != VIRTIO_VENDOR_ID) continue;
            
            uint32_t did = pci_read_config(bus, device, 0, 2); // Device ID
            
            char buf[64];
            sprintf(buf, "VirtIO Device Found: %04x:%04x (Bus %d, Dev %d)\n", vid, did, bus, device);
            kprint(buf);
            
            if (did == VIRTIO_PCI_DEVICE_ID_BLK) {
                // Initialize Block Driver
                extern void virtio_blk_setup(uint8_t bus, uint8_t slot, uint8_t func);
                virtio_blk_setup(bus, device, 0);
            } else if (did == VIRTIO_PCI_DEVICE_ID_9P) {
                extern void virtio_9p_setup(uint8_t bus, uint8_t slot, uint8_t func);
                virtio_9p_setup(bus, device, 0);
            }
        }
    }
}
