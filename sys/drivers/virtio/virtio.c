#include <drivers/virtio/virtio.h>
#include <arch/i386/cpu.h>
#include <arch/i386/pci.h>
#include <arch/x86-common/io.h>
#include <kern/console.h>
#include <kern/device.h>
#include <kern/driver.h>
#include <stdio.h>

static const device_id_t virtio_blk_pci_ids[] = {
    { VIRTIO_VENDOR_ID, VIRTIO_PCI_DEVICE_ID_BLK, 0, 0, 0 },
    { 0, 0, 0, 0, 0 },
};

static const device_id_t virtio_9p_pci_ids[] = {
    { VIRTIO_VENDOR_ID, VIRTIO_PCI_DEVICE_ID_9P, 0, 0, 0 },
    { 0, 0, 0, 0, 0 },
};

static int virtio_blk_pci_attach(struct device *dev) {
    pci_device_t *pdev = pci_find_device_by_kdev(dev);

    if (pdev == NULL) {
        return -1;
    }

    virtio_blk_setup(pdev->bus, pdev->slot, pdev->func);
    return 0;
}

static int virtio_9p_pci_attach(struct device *dev) {
    pci_device_t *pdev = pci_find_device_by_kdev(dev);

    if (pdev == NULL) {
        return -1;
    }

    virtio_9p_setup(pdev->bus, pdev->slot, pdev->func);
    return 0;
}

static int virtio_pci_detach(struct device *dev) {
    (void)dev;
    return 0;
}

static struct driver virtio_blk_pci_driver = {
    .name = "virtio-blk-pci",
    .id_table = virtio_blk_pci_ids,
    .attach = virtio_blk_pci_attach,
    .detach = virtio_pci_detach,
};

static struct driver virtio_9p_pci_driver = {
    .name = "virtio-9p-pci",
    .id_table = virtio_9p_pci_ids,
    .attach = virtio_9p_pci_attach,
    .detach = virtio_pci_detach,
};

// Find capabilities / BARs
uint16_t virtio_get_io_base(uint8_t bus, uint8_t slot, uint8_t func) {
    // Read BAR0 (Offset 0x10)
    uint32_t bar0 = pci_read(bus, slot, func, 0x10);
    if (bar0 & 1) { // IO Space
        return (uint16_t)(bar0 & 0xFFFC);
    }
    return 0;
}

void virtio_init(void) {
    static int virtio_drivers_registered;
    uint64_t start_tsc, end_tsc;
    start_tsc = i386_cpu_cycle_counter();

    if (!virtio_drivers_registered) {
        (void)driver_register(&virtio_blk_pci_driver, &pci_bus_type);
        (void)driver_register(&virtio_9p_pci_driver, &pci_bus_type);
        virtio_drivers_registered = 1;
    }

    end_tsc = i386_cpu_cycle_counter();
    char perf_buf[128];
    uint32_t diff_lo = (uint32_t)(end_tsc - start_tsc);
    snprintf(perf_buf, sizeof(perf_buf), "VirtIO Scan (Optimized): %u cycles\n", diff_lo);
    kprint(perf_buf);
}
