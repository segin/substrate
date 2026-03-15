#ifndef _SYS_KERN_PCI_H
#define _SYS_KERN_PCI_H

#include <stdint.h>

#include <kern/bus.h>
#include <kern/resource.h>

#define PCI_CONFIG_ADDRESS_PORT 0xCF8
#define PCI_CONFIG_DATA_PORT    0xCFC
#define PCI_CONFIG_ENABLE_BIT   0x80000000U
#define PCI_CONFIG_SPACE_SIZE   256U
#define PCI_EXT_CONFIG_SPACE_SIZE 4096U
#define PCI_CONFIG_COMMAND      0x04U
#define PCI_BAR_COUNT           6

#define PCI_BAR_NONE    0
#define PCI_BAR_IO      1
#define PCI_BAR_MEM32   2
#define PCI_BAR_MEM64   3

#define PCI_COMMAND_IO      0x0001U
#define PCI_COMMAND_MEMORY  0x0002U
#define PCI_COMMAND_MASTER  0x0004U

#define PCI_STATUS_CAP_LIST      0x0010U
#define PCI_CAP_ID_PM            0x01U
#define PCI_CAP_ID_MSI           0x05U
#define PCI_CAP_ID_PCIE          0x10U
#define PCI_CAP_ID_MSIX          0x11U

#define PCI_EXT_CAP_ID_AER       0x0001U
#define PCI_EXT_CAP_ID_SRIOV     0x0010U

#define PCI_IRQ_NONE             (-1)

struct device;

typedef struct pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t class_code;
    struct device *kdev;
    struct resource *bar_resource[PCI_BAR_COUNT];
    struct pci_device *next;
} pci_device_t;

extern struct bus_type pci_bus_type;

uint32_t pci_config_address(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset);
int pci_present(void);
void pci_ecam_configure(void *base, uint16_t segment, uint8_t start_bus, uint8_t end_bus);
void *pci_ecam_map(uint16_t segment, uint8_t bus, uint8_t slot, uint8_t func);

uint8_t pci_read_config8(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset);
uint16_t pci_read_config16(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset);
uint32_t pci_read_config32(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset);

void pci_write_config8(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset, uint8_t val);
void pci_write_config16(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset, uint16_t val);
void pci_write_config32(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset, uint32_t val);

void pci_init(void);
uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset);
void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset, uint32_t val);
int pci_scan_bus(uint8_t bus);
int pci_scan_bridge(pci_device_t *bridge);
pci_device_t *pci_device_create(uint8_t bus, uint8_t slot, uint8_t func);
int pci_find_capability(pci_device_t *dev, uint8_t cap_id);
int pci_find_ext_capability(pci_device_t *dev, uint16_t cap_id);
int pci_bar_type(pci_device_t *dev, int bar);
size_t pci_bar_size(pci_device_t *dev, int bar);
int pci_request_region(pci_device_t *dev, int bar, const char *name);
void *pci_iomap(pci_device_t *dev, int bar, size_t max_len);
int pci_get_irq(pci_device_t *dev);
int pci_enable_msi(pci_device_t *dev);
int pci_disable_msi(pci_device_t *dev);
int pci_enable_msix(pci_device_t *dev, int nvec);
void pci_hotplug_poll(void);
int pci_hotplug_add(uint8_t bus, uint8_t slot);
int pci_hotplug_remove(pci_device_t *dev);
void pci_scan(void);
pci_device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t *from);
pci_device_t *pci_find_device_by_kdev(struct device *kdev);
pci_device_t *pci_first_device(void);
pci_device_t *pci_next_device(pci_device_t *dev);
size_t pci_dump_devices(char *buf, size_t size);

#endif
