#ifndef _SYS_KERN_PCI_H
#define _SYS_KERN_PCI_H

#include <stdint.h>

#define PCI_CONFIG_ADDRESS_PORT 0xCF8
#define PCI_CONFIG_DATA_PORT    0xCFC
#define PCI_CONFIG_ENABLE_BIT   0x80000000U
#define PCI_CONFIG_SPACE_SIZE   256U

typedef struct pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t class_code;
    struct pci_device *next;
} pci_device_t;

uint32_t pci_config_address(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
int pci_present(void);

uint8_t pci_read_config8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_read_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint32_t pci_read_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

void pci_write_config8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t val);
void pci_write_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t val);
void pci_write_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);

void pci_init(void);
uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);
void pci_scan(void);
pci_device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t *from);

#endif
