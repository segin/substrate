#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kern/pci.h>
#include <sys/lock.h>

typedef void *pmap_t;

static uint8_t mock_config[256][32][8][4096];
void kprint(const char *str) { (void)str; }
void *kmalloc(size_t size) { return malloc(size); }
void kfree(void *ptr, size_t size) { (void)size; free(ptr); }
void spinlock_init(spinlock_t *lock, const char *name) { lock->locked = 0; lock->cpu_id = 0xFFFFFFFFU; lock->name = name; }
void spinlock_acquire(spinlock_t *lock) { (void)lock; }
bool spinlock_try_acquire(spinlock_t *lock) { (void)lock; return true; }
void spinlock_release(spinlock_t *lock) { (void)lock; }
bool spinlock_is_held(spinlock_t *lock) { (void)lock; return false; }
pmap_t pmap_kernel(void) { return (pmap_t)0x1; }
int pmap_enter(pmap_t pmap, uintptr_t va, uintptr_t pa, uint32_t prot, uint32_t flags) { (void)pmap; (void)va; (void)pa; (void)prot; (void)flags; return 0; }
void pmap_kremove(uintptr_t va) { (void)va; }
int pci_present(void) { return 1; }
void pci_ecam_configure(void *base, uint16_t segment, uint8_t start_bus, uint8_t end_bus) { (void)base; (void)segment; (void)start_bus; (void)end_bus; }
void *pci_ecam_map(uint16_t segment, uint8_t bus, uint8_t slot, uint8_t func) { (void)segment; return (void *)&mock_config[bus][slot][func][0]; }
uint8_t pci_read_config8(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset) { return mock_config[bus][slot][func][offset]; }
uint16_t pci_read_config16(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset) { return (uint16_t)pci_read_config8(bus,slot,func,offset) | ((uint16_t)pci_read_config8(bus,slot,func,offset+1) << 8); }
uint32_t pci_read_config32(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset) { return (uint32_t)pci_read_config16(bus,slot,func,offset) | ((uint32_t)pci_read_config16(bus,slot,func,offset+2) << 16); }
void pci_write_config8(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset, uint8_t val) { mock_config[bus][slot][func][offset] = val; }
void pci_write_config16(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset, uint16_t val) { pci_write_config8(bus,slot,func,offset,val & 0xff); pci_write_config8(bus,slot,func,offset+1,val >> 8); }
void pci_write_config32(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset, uint32_t val) { pci_write_config16(bus,slot,func,offset,val & 0xffff); pci_write_config16(bus,slot,func,offset+2,val >> 16); }
uint32_t pci_config_address(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset) { return PCI_CONFIG_ENABLE_BIT | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) | ((uint32_t)func << 8) | (offset & 0xFCU); }
uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset) { return pci_read_config32(bus, slot, func, offset); }
void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset, uint32_t val) { pci_write_config32(bus, slot, func, offset, val); }

#define HOST_TEST 1
#include "../../sys/kern/bus.c"
#include "../../sys/kern/driver.c"
#include "../../sys/kern/device.c"
#include "../../sys/kern/resource.c"
#include "../../sys/kern/ioremap.c"
#include "../../sys/kern/pci.c"

static void seed_device(void) {
    uint16_t vendor = 0x1234, device = 0x5678, status = PCI_STATUS_CAP_LIST;
    memset(mock_config, 0xff, sizeof(mock_config));
    memcpy(&mock_config[0][1][0][0x00], &vendor, 2);
    memcpy(&mock_config[0][1][0][0x02], &device, 2);
    memcpy(&mock_config[0][1][0][0x06], &status, 2);
    mock_config[0][1][0][0x34] = 0x50;
    mock_config[0][1][0][0x50] = PCI_CAP_ID_MSI;
    mock_config[0][1][0][0x51] = 0x60;
    mock_config[0][1][0][0x60] = PCI_CAP_ID_MSIX;
    mock_config[0][1][0][0x61] = 0x00;
    mock_config[0][1][0][0x3C] = 11;
    mock_config[0][1][0][0x3D] = 1;
    pci_write_config32(0,1,0,0x100, PCI_EXT_CAP_ID_AER | (0x120U << 20));
    pci_write_config32(0,1,0,0x120, PCI_EXT_CAP_ID_SRIOV);
}

int main(void) {
    seed_device();
    resource_init();
    pci_bus_initialized = 0;
    pci_devices_head = NULL;
    pci_devices_tail = NULL;
    pci_device_t *dev = pci_device_create(0, 1, 0);
    assert(dev != NULL);
    assert(pci_find_capability(dev, PCI_CAP_ID_MSI) == 0x50);
    assert(pci_find_capability(dev, PCI_CAP_ID_MSIX) == 0x60);
    assert(pci_find_ext_capability(dev, PCI_EXT_CAP_ID_AER) == 0x100);
    assert(pci_find_ext_capability(dev, PCI_EXT_CAP_ID_SRIOV) == 0x120);
    assert(pci_get_irq(dev) == 11);
    assert(pci_enable_msi(dev) == 0);
    assert((pci_read_config16(0,1,0,0x52) & 1U) == 1U);
    assert(pci_disable_msi(dev) == 0);
    assert((pci_read_config16(0,1,0,0x52) & 1U) == 0U);
    pci_write_config16(0,1,0,0x62, 3);
    assert(pci_enable_msix(dev, 2) == 0);
    return 0;
}
