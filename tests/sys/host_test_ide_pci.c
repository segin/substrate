#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <kern/device.h>
#include <kern/pci.h>

#include <drivers/storage/ide/ide.h>

static pci_device_t mock_devices[4];
static struct device mock_kdevs[4];
static uintptr_t mock_bars[4][PCI_BAR_COUNT];
static int mock_irqs[4];
static uint16_t mock_command[4];
static size_t mock_device_count;

static void reset_state(void) {
    memset(mock_devices, 0, sizeof(mock_devices));
    memset(mock_kdevs, 0, sizeof(mock_kdevs));
    memset(mock_bars, 0, sizeof(mock_bars));
    memset(mock_irqs, 0, sizeof(mock_irqs));
    memset(mock_command, 0, sizeof(mock_command));
    mock_device_count = 0;
}

static pci_device_t *add_ide_controller(uint8_t progif, int irq) {
    pci_device_t *pdev;
    struct device *kdev;

    assert(mock_device_count < (sizeof(mock_devices) / sizeof(mock_devices[0])));
    pdev = &mock_devices[mock_device_count];
    kdev = &mock_kdevs[mock_device_count];
    pdev->bus = 0;
    pdev->slot = 5;
    pdev->func = (uint8_t)mock_device_count;
    pdev->kdev = kdev;
    kdev->class = 0x01;
    kdev->subclass = 0x01;
    kdev->progif = progif;
    mock_irqs[mock_device_count] = irq;
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

int pci_get_irq(pci_device_t *dev) {
    return mock_irqs[mock_index_for(dev)];
}

uint32_t pci_read_config32(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset) {
    size_t i;
    uintptr_t value;

    (void)bus;
    (void)slot;
    assert(offset >= 0x10 && offset < 0x28);

    for (i = 0; i < mock_device_count; i++) {
        if (mock_devices[i].func == func) {
            value = mock_bars[i][(offset - 0x10) / 4];
            return (uint32_t)value;
        }
    }

    return 0xFFFFFFFFU;
}

uint16_t pci_read_config16(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset) {
    size_t i;

    (void)bus;
    (void)slot;

    if (offset != PCI_CONFIG_COMMAND) {
        return 0;
    }

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

    assert(0 && "unknown pci device for write_config16");
}

#include "../../sys/drivers/storage/ide/ide_pci.c"

static void test_first_controller_uses_primary_secondary_pair(void) {
    ide_channel_t channels[MAX_IDE_CHANNELS];
    uint8_t irq_shared[MAX_IDE_CHANNELS];
    pci_device_t *pdev;

    reset_state();
    memset(channels, 0, sizeof(channels));
    memset(irq_shared, 0, sizeof(irq_shared));
    pdev = add_ide_controller(0x8FU, 14);
    mock_bars[0][0] = 0x1F1U;
    mock_bars[0][1] = 0x3F5U;
    mock_bars[0][2] = 0x171U;
    mock_bars[0][3] = 0x375U;
    mock_bars[0][4] = 0xF001U;

    assert(ide_pci_configure_channels(channels, irq_shared) == 1);
    assert(channels[0].io_base == 0x1F0);
    assert(channels[0].ctrl_base == 0x3F6);
    assert(channels[1].io_base == 0x170);
    assert(channels[1].ctrl_base == 0x376);
    assert(channels[0].bm_base == 0xF000);
    assert(channels[1].bm_base == 0xF008);
    assert(channels[0].dma_capable == 1);
    assert(channels[1].dma_capable == 1);
    assert((mock_command[0] & (PCI_COMMAND_IO | PCI_COMMAND_MASTER)) ==
           (PCI_COMMAND_IO | PCI_COMMAND_MASTER));
    assert(channels[0].irq == 14);
    assert(channels[1].irq == 14);
    assert(irq_shared[0] == 1);
    assert(irq_shared[1] == 1);
    assert(pdev->kdev->progif == 0x8F);
}

static void test_second_controller_claims_tertiary_quaternary_pair(void) {
    ide_channel_t channels[MAX_IDE_CHANNELS];
    uint8_t irq_shared[MAX_IDE_CHANNELS];

    reset_state();
    memset(channels, 0, sizeof(channels));
    memset(irq_shared, 0, sizeof(irq_shared));

    add_ide_controller(0x8AU, 14);
    mock_bars[0][4] = 0xF001U;

    add_ide_controller(0x80U, 11);
    mock_bars[1][0] = 0xE801U;
    mock_bars[1][1] = 0xEE1U;
    mock_bars[1][2] = 0xE001U;
    mock_bars[1][3] = 0xE401U;
    mock_bars[1][4] = 0xD001U;

    assert(ide_pci_configure_channels(channels, irq_shared) == 2);
    assert(channels[2].io_base == 0xE800);
    assert(channels[2].ctrl_base == 0x0EE2);
    assert(channels[3].io_base == 0xE000);
    assert(channels[3].ctrl_base == 0x0E402);
    assert(channels[2].bm_base == 0xD000);
    assert(channels[3].bm_base == 0xD008);
    assert(channels[2].dma_capable == 1);
    assert(channels[3].dma_capable == 1);
    assert(channels[2].irq == 11);
    assert(channels[3].irq == 11);
    assert(irq_shared[2] == 1);
    assert(irq_shared[3] == 1);
    assert((mock_command[1] & (PCI_COMMAND_IO | PCI_COMMAND_MASTER)) ==
           (PCI_COMMAND_IO | PCI_COMMAND_MASTER));
}

static void test_compatibility_mode_still_imports_bus_master_bars(void) {
    ide_channel_t channels[MAX_IDE_CHANNELS];
    uint8_t irq_shared[MAX_IDE_CHANNELS];

    reset_state();
    memset(channels, 0, sizeof(channels));
    memset(irq_shared, 0, sizeof(irq_shared));

    channels[0].io_base = ATA_PRIMARY_IO;
    channels[0].ctrl_base = ATA_PRIMARY_CTRL;
    channels[0].irq = ATA_PRIMARY_IRQ;
    channels[1].io_base = ATA_SECONDARY_IO;
    channels[1].ctrl_base = ATA_SECONDARY_CTRL;
    channels[1].irq = ATA_SECONDARY_IRQ;

    add_ide_controller(0x80U, 14);
    mock_bars[0][4] = 0xF001U;

    assert(ide_pci_configure_channels(channels, irq_shared) == 1);
    assert(channels[0].io_base == ATA_PRIMARY_IO);
    assert(channels[0].ctrl_base == ATA_PRIMARY_CTRL);
    assert(channels[0].irq == ATA_PRIMARY_IRQ);
    assert(channels[1].io_base == ATA_SECONDARY_IO);
    assert(channels[1].ctrl_base == ATA_SECONDARY_CTRL);
    assert(channels[1].irq == ATA_SECONDARY_IRQ);
    assert(channels[0].bm_base == 0xF000);
    assert(channels[1].bm_base == 0xF008);
    assert(channels[0].dma_capable == 1);
    assert(channels[1].dma_capable == 1);
    assert((mock_command[0] & (PCI_COMMAND_IO | PCI_COMMAND_MASTER)) ==
           (PCI_COMMAND_IO | PCI_COMMAND_MASTER));
    assert(irq_shared[0] == 0);
    assert(irq_shared[1] == 0);
}

static void test_ignores_non_ide_devices_and_channel_overflow(void) {
    ide_channel_t channels[MAX_IDE_CHANNELS];
    uint8_t irq_shared[MAX_IDE_CHANNELS];
    pci_device_t *pdev;

    reset_state();
    memset(channels, 0, sizeof(channels));
    memset(irq_shared, 0, sizeof(irq_shared));

    pdev = add_ide_controller(0x80U, 14);
    mock_bars[0][0] = 0x1F1U;
    mock_bars[0][1] = 0x3F5U;
    pdev->kdev->subclass = 0x00;

    add_ide_controller(0x80U, 15);
    add_ide_controller(0x80U, 11);
    add_ide_controller(0x80U, 10);

    mock_bars[2][0] = 0xE801U;
    mock_bars[2][1] = 0xEE1U;
    mock_bars[2][2] = 0xE001U;
    mock_bars[2][3] = 0xE401U;
    mock_bars[2][4] = 0xD001U;

    assert(ide_pci_configure_channels(channels, irq_shared) == 2);
    assert(channels[0].io_base == 0);
    assert(channels[1].io_base == 0);
    assert(channels[0].irq == 0);
    assert(channels[1].irq == 0);
    assert(channels[2].irq == 11);
    assert(channels[3].irq == 11);
    assert(channels[2].io_base == 0xE800);
    assert(channels[3].io_base == 0xE000);
}

int main(void) {
    test_first_controller_uses_primary_secondary_pair();
    test_second_controller_claims_tertiary_quaternary_pair();
    test_compatibility_mode_still_imports_bus_master_bars();
    test_ignores_non_ide_devices_and_channel_overflow();
    puts("host_test_ide_pci: PASS");
    return 0;
}
