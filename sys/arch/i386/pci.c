#include <arch/i386/pci.h>
#include <arch/x86-common/include/io.h>
#include <kern/console.h>
#include <stdio.h>

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

static pci_device_t *pci_devices_head = NULL;
static pci_device_t *pci_devices_tail = NULL;

extern void *kmalloc(size_t size);

void pci_check_device(uint8_t bus, uint8_t device) {
    uint8_t function = 0;
 
    uint32_t vendorID = pci_read(bus, device, function, 0);
    if(vendorID == 0xFFFF) return; // Device doesn't exist

    uint32_t deviceID = pci_read(bus, device, function, 2);
    // Lower 8 bits of 0x0A is sub-class, 0x0B is class
    uint32_t classCode = pci_read(bus, device, function, 0x0A);
    
    char buf[64];
    sprintf(buf, "PCI %02x:%02x.0 %04x:%04x [%04x]\n", 
            bus, device, vendorID, deviceID, classCode);
    kprint(buf);

    pci_device_t *dev = (pci_device_t *)kmalloc(sizeof(pci_device_t));
    if (dev) {
        dev->bus = bus;
        dev->slot = device;
        dev->func = function;
        dev->vendor_id = (uint16_t)vendorID;
        dev->device_id = (uint16_t)deviceID;
        dev->class_code = classCode;
        dev->next = NULL;

        if (pci_devices_tail) {
            pci_devices_tail->next = dev;
        } else {
            pci_devices_head = dev;
        }
        pci_devices_tail = dev;
    }
}

void pci_scan(void) {
    for(uint16_t bus = 0; bus < 256; bus++) {
        for(uint8_t device = 0; device < 32; device++) {
            pci_check_device(bus, device);
        }
    }
}

pci_device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t *from) {
    pci_device_t *curr = from ? from->next : pci_devices_head;
    while (curr) {
        if (curr->vendor_id == vendor_id && (device_id == 0xFFFF || curr->device_id == device_id)) {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

void pci_init(void) {
    kprint("Scanning PCI Bus...\n");
    pci_scan();
}
