#include <drivers/virtio/virtio.h>
#include <arch/i386/pci.h>
#include <arch/x86-common/include/io.h>
#include <kern/console.h>
#include <stdio.h>

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
    uint64_t start_tsc, end_tsc;
    __asm__ volatile("rdtsc" : "=A"(start_tsc));

    // Use PCI subsystem to find devices instead of rescanning
    pci_device_t *dev = NULL;
    while ((dev = pci_find_device(VIRTIO_VENDOR_ID, 0xFFFF, dev))) {
        uint32_t vid = dev->vendor_id;
        uint32_t did = dev->device_id;

        char buf[64];
        sprintf(buf, "VirtIO Device Found: %04x:%04x (Bus %d, Dev %d)\n", vid, did, dev->bus, dev->slot);
        kprint(buf);

        if (did == VIRTIO_PCI_DEVICE_ID_BLK) {
            // Initialize Block Driver
            extern void virtio_blk_setup(uint8_t bus, uint8_t slot, uint8_t func);
            virtio_blk_setup(dev->bus, dev->slot, dev->func);
        } else if (did == VIRTIO_PCI_DEVICE_ID_9P) {
            extern void virtio_9p_setup(uint8_t bus, uint8_t slot, uint8_t func);
            virtio_9p_setup(dev->bus, dev->slot, dev->func);
        }
    }

    __asm__ volatile("rdtsc" : "=A"(end_tsc));
    char perf_buf[128];
    uint32_t diff_lo = (uint32_t)(end_tsc - start_tsc);
    sprintf(perf_buf, "VirtIO Scan (Optimized): %u cycles\n", diff_lo);
    kprint(perf_buf);
}
