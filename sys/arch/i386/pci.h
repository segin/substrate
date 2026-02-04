#ifndef _PCI_H
#define _PCI_H

#include <stdint.h>

typedef struct pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t class_code;
    struct pci_device *next;
} pci_device_t;

void pci_init(void);
uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);
void pci_scan(void);

pci_device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t *from);

#endif
