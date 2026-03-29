#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <kern/device.h>
#include <kern/pci.h>

#include <drivers/storage/nvme/nvme.h>

static pci_device_t mock_devices[4];
static struct device mock_kdevs[4];
static uint16_t mock_command[4];
static uint8_t mock_mmio[4][64];
static void *mock_mmio_map[4];
static size_t mock_device_count;
static int kprint_calls;

static void reset_state(void) {
    memset(mock_devices, 0, sizeof(mock_devices));
    memset(mock_kdevs, 0, sizeof(mock_kdevs));
    memset(mock_command, 0, sizeof(mock_command));
    memset(mock_mmio, 0, sizeof(mock_mmio));
    memset(mock_mmio_map, 0, sizeof(mock_mmio_map));
    mock_device_count = 0;
    kprint_calls = 0;
}

static pci_device_t *add_pci_device(uint16_t class_code, uint16_t subclass,
                                    uint8_t progif, uint16_t vendor_id,
                                    uint16_t device_id) {
    pci_device_t *pdev;
    struct device *kdev;

    assert(mock_device_count < (sizeof(mock_devices) / sizeof(mock_devices[0])));
    pdev = &mock_devices[mock_device_count];
    kdev = &mock_kdevs[mock_device_count];

    pdev->bus = 0;
    pdev->slot = (uint8_t)(4 + mock_device_count);
    pdev->func = (uint8_t)mock_device_count;
    pdev->vendor_id = vendor_id;
    pdev->device_id = device_id;
    pdev->kdev = kdev;

    kdev->class = class_code;
    kdev->subclass = subclass;
    kdev->progif = progif;

    mock_mmio_map[mock_device_count] = mock_mmio[mock_device_count];
    mock_device_count++;
    return pdev;
}

static size_t mock_index_for(const pci_device_t *pdev) {
    size_t i;

    for (i = 0; i < mock_device_count; i++) {
        if (&mock_devices[i] == pdev) {
            return i;
        }
    }
    assert(0 && "unknown pci device");
    return 0;
}

pci_device_t *pci_first_device(void) {
    if (mock_device_count == 0) {
        return NULL;
    }
    return &mock_devices[0];
}

pci_device_t *pci_next_device(pci_device_t *dev) {
    size_t i;

    if (dev == NULL) {
        return NULL;
    }

    for (i = 0; i < mock_device_count; i++) {
        if (&mock_devices[i] == dev) {
            if (i + 1 < mock_device_count) {
                return &mock_devices[i + 1];
            }
            break;
        }
    }

    return NULL;
}

uint16_t pci_read_config16(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset) {
    size_t i;

    (void)bus;
    (void)slot;
    assert(offset == PCI_CONFIG_COMMAND);

    for (i = 0; i < mock_device_count; i++) {
        if (mock_devices[i].func == func) {
            return mock_command[i];
        }
    }

    return 0;
}

void pci_write_config16(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset, uint16_t val) {
    size_t i;

    (void)bus;
    (void)slot;
    assert(offset == PCI_CONFIG_COMMAND);

    for (i = 0; i < mock_device_count; i++) {
        if (mock_devices[i].func == func) {
            mock_command[i] = val;
            return;
        }
    }

    assert(0 && "unknown pci function");
}

void *pci_iomap(pci_device_t *dev, int bar, size_t max_len) {
    (void)bar;
    (void)max_len;
    return mock_mmio_map[mock_index_for(dev)];
}

void kprint(const char *str) {
    (void)str;
    kprint_calls++;
}

#include "../../sys/drivers/storage/nvme/nvme.c"

static void test_decode_cap_extracts_timeout_and_stride(void) {
    nvme_capability_t cap;
    uint64_t raw = 0;

    raw |= 0x1234ULL;
    raw |= (uint64_t)10 << 24;
    raw |= (uint64_t)3 << 32;

    assert(nvme_decode_cap(raw, &cap) == 0);
    assert(cap.mqes == 0x1234);
    assert(cap.timeout == 10);
    assert(cap.timeout_ms == 5000);
    assert(cap.doorbell_stride == 3);
    assert(cap.doorbell_stride_bytes == 32);
}

static void test_scan_filters_nvme_functions_and_records_cap(void) {
    nvme_controller_t ctrls[2];
    pci_device_t *pdev;
    uint64_t cap_raw = 0;

    reset_state();

    add_pci_device(0x01, 0x06, 0x01, 0x8086, 0x2922);
    pdev = add_pci_device(0x01, 0x08, 0x02, 0x144d, 0xa808);

    cap_raw |= 0x00ffULL;
    cap_raw |= (uint64_t)7 << 24;
    cap_raw |= (uint64_t)2 << 32;
    memcpy(mock_mmio[mock_index_for(pdev)], &cap_raw, sizeof(cap_raw));

    assert(nvme_scan_controllers(ctrls, 2) == 1);
    assert(ctrls[0].present == 1);
    assert(ctrls[0].bus == pdev->bus);
    assert(ctrls[0].slot == pdev->slot);
    assert(ctrls[0].func == pdev->func);
    assert(ctrls[0].vendor_id == 0x144d);
    assert(ctrls[0].device_id == 0xa808);
    assert(ctrls[0].cap_raw == cap_raw);
    assert(ctrls[0].cap.timeout == 7);
    assert(ctrls[0].cap.timeout_ms == 3500);
    assert(ctrls[0].cap.doorbell_stride_bytes == 16);
    assert((mock_command[mock_index_for(pdev)] & (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) ==
           (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER));
    assert(kprint_calls == 1);
}

static void test_init_publishes_scanned_controllers(void) {
    pci_device_t *pdev;
    uint64_t cap_raw = 0;
    const nvme_controller_t *ctrl;

    reset_state();
    pdev = add_pci_device(0x01, 0x08, 0x02, 0x8086, 0xf1a5);
    cap_raw |= 0x0010ULL;
    cap_raw |= (uint64_t)4 << 24;
    memcpy(mock_mmio[mock_index_for(pdev)], &cap_raw, sizeof(cap_raw));

    nvme_init();
    assert(nvme_controller_count() == 1);
    ctrl = nvme_get_controller(0);
    assert(ctrl != NULL);
    assert(ctrl->device_id == 0xf1a5);
    assert(ctrl->cap.timeout_ms == 2000);
}

int main(void) {
    test_decode_cap_extracts_timeout_and_stride();
    test_scan_filters_nvme_functions_and_records_cap();
    test_init_publishes_scanned_controllers();
    puts("host_test_nvme: PASS");
    return 0;
}
