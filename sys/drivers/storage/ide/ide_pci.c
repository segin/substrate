#include <stddef.h>
#include <stdint.h>

#include <kern/device.h>
#include <kern/pci.h>

#include <drivers/storage/ide/ide.h>

static uintptr_t ide_pci_bar_base(const pci_device_t *pdev, int bar) {
    uint32_t value;

    if (pdev == NULL || bar < 0 || bar >= PCI_BAR_COUNT) {
        return 0;
    }

    value = pci_read_config32(pdev->bus, pdev->slot, pdev->func,
                              (uint16_t)(0x10 + bar * 4));
    if (value == 0 || value == 0xFFFFFFFFU) {
        return 0;
    }

    if (value & 1U) {
        return (uintptr_t)(value & ~0x3U);
    }

    return (uintptr_t)(value & ~0xFU);
}

static void ide_pci_apply_channel(ide_channel_t *channel, uint8_t *irq_shared,
                                  const pci_device_t *pdev, int pci_irq,
                                  uintptr_t io, uintptr_t ctrl, uintptr_t bm) {
    if (channel == NULL || irq_shared == NULL || pdev == NULL) {
        return;
    }

    if (io != 0) {
        channel->io_base = (uint16_t)io;
    }
    if (ctrl != 0) {
        channel->ctrl_base = (uint16_t)(ctrl + 2);
    }
    if (bm != 0) {
        channel->bm_base = (uint16_t)bm;
        channel->dma_capable = 1;
    }
    if (pci_irq != PCI_IRQ_NONE) {
        channel->irq = (uint8_t)pci_irq;
        *irq_shared = 1;
    }
}

size_t ide_pci_configure_channels(ide_channel_t channels[MAX_IDE_CHANNELS],
                                  uint8_t irq_shared[MAX_IDE_CHANNELS]) {
    pci_device_t *pdev;
    size_t configured_pairs = 0;

    if (channels == NULL || irq_shared == NULL) {
        return 0;
    }

    for (pdev = pci_first_device(); pdev != NULL; pdev = pci_next_device(pdev)) {
        uintptr_t bm_base;
        uintptr_t primary_io;
        uintptr_t primary_ctrl;
        uintptr_t secondary_io;
        uintptr_t secondary_ctrl;
        size_t pair_base;
        int pci_irq;
        int primary_native;
        int secondary_native;
        int use_bar_layout;

        if (pdev->kdev == NULL) {
            continue;
        }
        if (pdev->kdev->class != 0x01 || pdev->kdev->subclass != 0x01) {
            continue;
        }

        pair_base = configured_pairs * 2;
        if (pair_base >= MAX_IDE_CHANNELS) {
            break;
        }

        pci_irq = pci_get_irq(pdev);
        primary_native = (pdev->kdev->progif & 0x01) != 0;
        secondary_native = (pdev->kdev->progif & 0x04) != 0;
        use_bar_layout = (pair_base >= 2);

        primary_io = ide_pci_bar_base(pdev, 0);
        primary_ctrl = ide_pci_bar_base(pdev, 1);
        secondary_io = ide_pci_bar_base(pdev, 2);
        secondary_ctrl = ide_pci_bar_base(pdev, 3);
        bm_base = ide_pci_bar_base(pdev, 4);

        if (primary_native || use_bar_layout) {
            ide_pci_apply_channel(&channels[pair_base], &irq_shared[pair_base],
                                  pdev, pci_irq, primary_io, primary_ctrl,
                                  bm_base);
        }

        if (pair_base + 1 < MAX_IDE_CHANNELS &&
            (secondary_native || use_bar_layout)) {
            ide_pci_apply_channel(&channels[pair_base + 1],
                                  &irq_shared[pair_base + 1], pdev, pci_irq,
                                  secondary_io, secondary_ctrl,
                                  bm_base == 0 ? 0 : (bm_base + 8));
        }

        configured_pairs++;
    }

    return configured_pairs;
}
