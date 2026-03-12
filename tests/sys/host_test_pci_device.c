#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
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

void spinlock_acquire(spinlock_t *lock) {
    (void)lock;
}

bool spinlock_try_acquire(spinlock_t *lock) {
    (void)lock;
    return true;
}

void spinlock_release(spinlock_t *lock) {
    (void)lock;
}

bool spinlock_is_held(spinlock_t *lock) {
    (void)lock;
    return false;
}

int pci_present(void) {
    return mock_pci_available;
}

uint8_t pci_read_config8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    if (!mock_pci_available || slot >= 32 || func >= 8) {
        return 0xFFU;
    }
    return mock_config[bus][slot][func][offset];
}

uint16_t pci_read_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return (uint16_t)pci_read_config8(bus, slot, func, offset) |
        ((uint16_t)pci_read_config8(bus, slot, func, (uint8_t)(offset + 1U)) << 8);
}

uint32_t pci_read_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return (uint32_t)pci_read_config16(bus, slot, func, offset) |
        ((uint32_t)pci_read_config16(bus, slot, func, (uint8_t)(offset + 2U)) << 16);
}

void pci_write_config8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t value) {
    mock_config[bus][slot][func][offset] = value;
}

void pci_write_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value) {
    pci_write_config8(bus, slot, func, offset, (uint8_t)(value & 0xFFU));
    pci_write_config8(bus, slot, func, (uint8_t)(offset + 1U), (uint8_t)(value >> 8));
}

void pci_write_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    pci_write_config16(bus, slot, func, offset, (uint16_t)(value & 0xFFFFU));
    pci_write_config16(bus, slot, func, (uint8_t)(offset + 2U), (uint16_t)(value >> 16));
}

uint32_t pci_config_address(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return PCI_CONFIG_ENABLE_BIT |
        ((uint32_t)bus << 16) |
        ((uint32_t)slot << 11) |
        ((uint32_t)func << 8) |
        (offset & 0xFCU);
}

uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return pci_read_config16(bus, slot, func, offset);
}

void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    pci_write_config32(bus, slot, func, offset, val);
}

#include "../../sys/kern/bus.c"
#include "../../sys/kern/driver.c"
#include "../../sys/kern/device.c"
#include "../../sys/kern/pci.c"

static void reset_state(void) {
    memset(mock_config, 0xFF, sizeof(mock_config));
    mock_pci_available = 1;
    pci_devices_clear();
    memset(&pci_bus_type, 0, sizeof(pci_bus_type));
    pci_bus_initialized = 0;
}

static void mock_set_device(uint8_t bus, uint8_t slot, uint8_t func, uint16_t vendor,
                            uint16_t device, uint8_t class_id, uint8_t subclass,
                            uint8_t progif, uint8_t header_type) {
    memcpy(&mock_config[bus][slot][func][0x00], &vendor, sizeof(vendor));
    memcpy(&mock_config[bus][slot][func][0x02], &device, sizeof(device));
    mock_config[bus][slot][func][0x09] = progif;
    mock_config[bus][slot][func][0x0A] = subclass;
    mock_config[bus][slot][func][0x0B] = class_id;
    mock_config[bus][slot][func][0x0E] = header_type;
}

static void test_pci_device_create_populates_pci_and_device_state(void) {
    pci_device_t *pdev;

    reset_state();
    mock_set_device(0, 7, 0, 0x1AF4, 0x1001, 0x01, 0x00, 0x00, 0x00);

    pdev = pci_device_create(0, 7, 0);
    assert(pdev != NULL);
    assert(pdev->vendor_id == 0x1AF4);
    assert(pdev->device_id == 0x1001);
    assert(pdev->class_code == 0x0100);
    assert(pdev->kdev != NULL);
    assert(strcmp(pdev->kdev->name, "pci00:07.0") == 0);
    assert(pdev->kdev->bus == &pci_bus_type);
    assert(pdev->kdev->vendor_id == 0x1AF4);
    assert(pdev->kdev->device_id == 0x1001);
    assert(pdev->kdev->class == 0x01);
    assert(pdev->kdev->subclass == 0x00);
    assert(pdev->kdev->progif == 0x00);
    assert(pci_bus_type.devices_list == pdev->kdev);
}

static void test_pci_device_create_rejects_absent_function(void) {
    reset_state();
    assert(pci_device_create(0, 0, 0) == NULL);
}

int main(void) {
    test_pci_device_create_populates_pci_and_device_state();
    test_pci_device_create_rejects_absent_function();
    puts("host_test_pci_device: PASS");
    return 0;
}
