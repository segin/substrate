#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kern/pci.h>

static uint8_t mock_config[256][32][8][256];
static int mock_pci_available = 1;

void kprint(const char *str) {
    (void)str;
}

void *kmalloc(size_t size) {
    return malloc(size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

void spinlock_init(spinlock_t *lock, const char *name) {
    lock->locked = 0;
    lock->cpu_id = 0xFFFFFFFFU;
    lock->name = name;
}
void spinlock_acquire(spinlock_t *lock) { (void)lock; }
bool spinlock_try_acquire(spinlock_t *lock) { (void)lock; return true; }
void spinlock_release(spinlock_t *lock) { (void)lock; }
bool spinlock_is_held(spinlock_t *lock) { (void)lock; return false; }

int pci_present(void) {
    return mock_pci_available;
}

uint8_t pci_read_config8(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset) {
    if (!mock_pci_available || slot >= 32 || func >= 8) {
        return 0xFFU;
    }
    return mock_config[bus][slot][func][offset];
}

uint16_t pci_read_config16(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset) {
    return (uint16_t)pci_read_config8(bus, slot, func, offset) |
        ((uint16_t)pci_read_config8(bus, slot, func, (uint8_t)(offset + 1U)) << 8);
}

uint32_t pci_read_config32(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset) {
    return (uint32_t)pci_read_config16(bus, slot, func, offset) |
        ((uint32_t)pci_read_config16(bus, slot, func, (uint8_t)(offset + 2U)) << 16);
}

void pci_write_config8(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset, uint8_t value) {
    if (!mock_pci_available || slot >= 32 || func >= 8) {
        return;
    }
    mock_config[bus][slot][func][offset] = value;
}

void pci_write_config16(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset, uint16_t value) {
    pci_write_config8(bus, slot, func, offset, (uint8_t)(value & 0xFFU));
    pci_write_config8(bus, slot, func, (uint8_t)(offset + 1U), (uint8_t)(value >> 8));
}

void pci_write_config32(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset, uint32_t value) {
    pci_write_config16(bus, slot, func, offset, (uint16_t)(value & 0xFFFFU));
    pci_write_config16(bus, slot, func, (uint8_t)(offset + 2U), (uint16_t)(value >> 16));
}

uint32_t pci_config_address(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset) {
    return PCI_CONFIG_ENABLE_BIT |
        ((uint32_t)bus << 16) |
        ((uint32_t)slot << 11) |
        ((uint32_t)func << 8) |
        (offset & 0xFCU);
}

uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset) {
    return pci_read_config16(bus, slot, func, offset);
}

void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset, uint32_t val) {
    pci_write_config32(bus, slot, func, offset, val);
}

typedef void *pmap_t;
pmap_t pmap_kernel(void) { return (pmap_t)0x1; }
int pmap_enter(pmap_t pmap, uintptr_t va, uintptr_t pa, uint32_t prot, uint32_t flags) {
    (void)pmap; (void)va; (void)pa; (void)prot; (void)flags; return 0;
}
void pmap_kremove(uintptr_t va) { (void)va; }

#include "../../sys/kern/bus.c"
#include "../../sys/kern/driver.c"
#include "../../sys/kern/device.c"
#include "../../sys/kern/resource.c"
#include "../../sys/kern/ioremap.c"
#include "../../sys/kern/bus.c"
#include "../../sys/kern/driver.c"
#include "../../sys/kern/device.c"
#include "../../sys/kern/resource.c"
#include "../../sys/kern/ioremap.c"
#include "../../sys/kern/pci.c"

static void reset_bus(void) {
    memset(mock_config, 0xFF, sizeof(mock_config));
    mock_pci_available = 1;
    pci_bus_initialized = 0;
    pci_devices_clear();
    resource_init();
}

static void mock_set_device(uint8_t bus, uint8_t slot, uint8_t func, uint16_t vendor,
                            uint16_t device, uint16_t class_code, uint8_t header_type) {
    memcpy(&mock_config[bus][slot][func][0x00], &vendor, sizeof(vendor));
    memcpy(&mock_config[bus][slot][func][0x02], &device, sizeof(device));
    memcpy(&mock_config[bus][slot][func][0x0A], &class_code, sizeof(class_code));
    mock_config[bus][slot][func][0x0E] = header_type;
}

static void test_scan_empty_bus_returns_zero(void) {
    reset_bus();
    assert(pci_scan_bus(0) == 0);
    assert(pci_find_device(0x1234, 0x5678, NULL) == NULL);
}

static void test_scan_records_single_function_device(void) {
    pci_device_t *dev;

    reset_bus();
    mock_set_device(0, 5, 0, 0x1234, 0x5678, 0x0101, 0x00);

    assert(pci_scan_bus(0) == 1);
    dev = pci_find_device(0x1234, 0x5678, NULL);
    assert(dev != NULL);
    assert(dev->bus == 0);
    assert(dev->slot == 5);
    assert(dev->func == 0);
    assert(dev->class_code == 0x0101);
}

static void test_scan_detects_multifunction_slot(void) {
    pci_device_t *first;
    pci_device_t *second;

    reset_bus();
    mock_set_device(0, 2, 0, 0x1111, 0xAAAA, 0x0106, 0x80);
    mock_set_device(0, 2, 3, 0x1111, 0xBBBB, 0x0200, 0x00);

    assert(pci_scan_bus(0) == 2);
    first = pci_find_device(0x1111, 0xAAAA, NULL);
    second = pci_find_device(0x1111, 0xBBBB, NULL);
    assert(first != NULL);
    assert(second != NULL);
    assert(second->func == 3);
}

static void test_scan_skips_non_pci_systems(void) {
    reset_bus();
    mock_pci_available = 0;
    assert(pci_scan_bus(0) == 0);
    assert(pci_find_device(0x1234, 0x5678, NULL) == NULL);
}

static void test_scan_recurses_across_bridge_bus_range(void) {
    pci_device_t *bridge;
    pci_device_t *downstream;

    reset_bus();
    mock_set_device(0, 1, 0, 0x8086, 0x244E, 0x0604, 0x01);
    mock_config[0][1][0][0x19] = 2;
    mock_config[0][1][0][0x1A] = 2;
    mock_set_device(2, 4, 0, 0x1AF4, 0x1001, 0x0100, 0x00);

    assert(pci_scan_bus(0) == 2);
    bridge = pci_find_device(0x8086, 0x244E, NULL);
    downstream = pci_find_device(0x1AF4, 0x1001, NULL);
    assert(bridge != NULL);
    assert(downstream != NULL);
    assert(downstream->bus == 2);
}

int main(void) {
    test_scan_empty_bus_returns_zero();
    test_scan_records_single_function_device();
    test_scan_detects_multifunction_slot();
    test_scan_skips_non_pci_systems();
    test_scan_recurses_across_bridge_bus_range();
    puts("host_test_pci_scan: PASS");
    return 0;
}
