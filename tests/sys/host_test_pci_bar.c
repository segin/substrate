#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kern/pci.h>

static uint8_t mock_config[256][32][8][256];
static uint8_t probe_low_active;
static uint8_t probe_high_active;
static uint32_t probe_low_mask;
static uint32_t probe_high_mask;

void kprint(const char *str) { (void)str; }
void *kmalloc(size_t size) { return malloc(size); }
void kfree(void *ptr, size_t size) { (void)size; free(ptr); }

void spinlock_init(spinlock_t *lock, const char *name) {
    lock->locked = 0;
    lock->cpu_id = 0xFFFFFFFFU;
    lock->name = name;
}
void spinlock_acquire(spinlock_t *lock) { (void)lock; }
bool spinlock_try_acquire(spinlock_t *lock) { (void)lock; return true; }
void spinlock_release(spinlock_t *lock) { (void)lock; }
bool spinlock_is_held(spinlock_t *lock) { (void)lock; return false; }

int pci_present(void) { return 1; }

uint8_t pci_read_config8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return mock_config[bus][slot][func][offset];
}

uint16_t pci_read_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return (uint16_t)pci_read_config8(bus, slot, func, offset) |
        ((uint16_t)pci_read_config8(bus, slot, func, (uint8_t)(offset + 1)) << 8);
}

uint32_t pci_read_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    if (offset == 0x10 && probe_low_active) {
        return probe_low_mask;
    }
    if (offset == 0x14 && probe_high_active) {
        return probe_high_mask;
    }
    return (uint32_t)pci_read_config16(bus, slot, func, offset) |
        ((uint32_t)pci_read_config16(bus, slot, func, (uint8_t)(offset + 2)) << 16);
}

void pci_write_config8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t value) {
    mock_config[bus][slot][func][offset] = value;
}

void pci_write_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value) {
    pci_write_config8(bus, slot, func, offset, (uint8_t)(value & 0xFF));
    pci_write_config8(bus, slot, func, (uint8_t)(offset + 1), (uint8_t)(value >> 8));
}

void pci_write_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    if (offset == 0x10 && value == 0xFFFFFFFFU) {
        probe_low_active = 1;
        return;
    }
    if (offset == 0x14 && value == 0xFFFFFFFFU) {
        probe_high_active = 1;
        return;
    }
    if (offset == 0x10) {
        probe_low_active = 0;
    }
    if (offset == 0x14) {
        probe_high_active = 0;
    }
    pci_write_config16(bus, slot, func, offset, (uint16_t)(value & 0xFFFF));
    pci_write_config16(bus, slot, func, (uint8_t)(offset + 2), (uint16_t)(value >> 16));
}

uint32_t pci_config_address(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return PCI_CONFIG_ENABLE_BIT | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
           ((uint32_t)func << 8) | (offset & 0xFCU);
}

uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return pci_read_config16(bus, slot, func, offset);
}
void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    pci_write_config32(bus, slot, func, offset, val);
}

typedef void *pmap_t;
pmap_t pmap_kernel(void) { return (pmap_t)0x1; }
int pmap_enter(pmap_t pmap, uintptr_t va, uintptr_t pa, uint32_t prot, uint32_t flags) {
    (void)pmap; (void)va; (void)pa; (void)prot; (void)flags; return 0;
}
void pmap_kremove(uintptr_t va) { (void)va; }

#define HOST_TEST 1
#include "../../sys/kern/bus.c"
#include "../../sys/kern/driver.c"
#include "../../sys/kern/device.c"
#include "../../sys/kern/resource.c"
#include "../../sys/kern/ioremap.c"
#include "../../sys/kern/pci.c"

static void reset_state(void) {
    memset(mock_config, 0, sizeof(mock_config));
    memset(&pci_bus_type, 0, sizeof(pci_bus_type));
    pci_bus_initialized = 0;
    pci_devices_clear();
    resource_init();
    ioremap_regions = NULL;
    ioremap_next = IOREMAP_BASE;
    probe_low_active = 0;
    probe_high_active = 0;
    probe_low_mask = 0;
    probe_high_mask = 0;
}

static pci_device_t *make_device(uint32_t bar0, uint32_t bar1) {
    uint16_t vendor = 0x1234;
    uint16_t device = 0x5678;
    reset_state();
    memcpy(&mock_config[0][1][0][0x00], &vendor, 2);
    memcpy(&mock_config[0][1][0][0x02], &device, 2);
    pci_write_config32(0, 1, 0, 0x10, bar0);
    pci_write_config32(0, 1, 0, 0x14, bar1);
    return pci_device_create(0, 1, 0);
}

static void test_bar_type_detection(void) {
    pci_device_t *dev = make_device(0x0000C001U, 0);
    assert(pci_bar_type(dev, 0) == PCI_BAR_IO);
    dev = make_device(0xFEBF0000U, 0);
    assert(pci_bar_type(dev, 0) == PCI_BAR_MEM32);
    dev = make_device(0x00000004U, 0x00000001U);
    assert(pci_bar_type(dev, 0) == PCI_BAR_MEM64);
}

static void test_bar_size_probe_and_region_request(void) {
    pci_device_t *dev = make_device(0xFEBF0000U, 0);
    probe_low_mask = 0xFFFFF000U;
    assert(pci_bar_size(dev, 0) == 0x1000U);
    assert(pci_request_region(dev, 0, "mmio") == 0);
    assert(dev->bar_resource[0] != NULL);
}

static void test_pci_iomap_returns_mapping(void) {
    pci_device_t *dev = make_device(0xFEBF0000U, 0);
    probe_low_mask = 0xFFFFF000U;
    assert(pci_iomap(dev, 0, 0x1000) != NULL);
}

int main(void) {
    test_bar_type_detection();
    test_bar_size_probe_and_region_request();
    test_pci_iomap_returns_mapping();
    puts("host_test_pci_bar: PASS");
    return 0;
}
