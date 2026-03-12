#include <arch/i386/pci.h>
#include <arch/x86-common/io.h>
#include <kern/console.h>
#include <stdio.h>

extern void *kmalloc(size_t size);

#ifndef HOST_TEST
static void pci_select_address(uint32_t address) {
    outl(PCI_CONFIG_ADDRESS_PORT, address);
}

static uint32_t pci_config_readback(void) {
    return inl(PCI_CONFIG_ADDRESS_PORT);
}

static uint32_t pci_io_read32(uint32_t address) {
    pci_select_address(address);
    return inl(PCI_CONFIG_DATA_PORT);
}

static void pci_io_write32(uint32_t address, uint32_t value) {
    pci_select_address(address);
    outl(PCI_CONFIG_DATA_PORT, value);
}
#else
extern void pci_test_config_select(uint32_t address);
extern uint32_t pci_test_config_readback(void);
extern uint32_t pci_test_data_read32(void);
extern void pci_test_data_write32(uint32_t value);

static void pci_select_address(uint32_t address) {
    pci_test_config_select(address);
}

static uint32_t pci_config_readback(void) {
    return pci_test_config_readback();
}

static uint32_t pci_io_read32(uint32_t address) {
    pci_select_address(address);
    return pci_test_data_read32();
}

static void pci_io_write32(uint32_t address, uint32_t value) {
    pci_select_address(address);
    pci_test_data_write32(value);
}
#endif

static pci_device_t *pci_devices_head = NULL;
static pci_device_t *pci_devices_tail = NULL;

int pci_present(void) {
    uint32_t saved = pci_config_readback();

    pci_select_address(PCI_CONFIG_ENABLE_BIT);
    if (pci_config_readback() != PCI_CONFIG_ENABLE_BIT) {
        pci_select_address(saved);
        return 0;
    }

    pci_select_address(saved);
    return 1;
}

static uint32_t pci_read_aligned_config32(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset) {
    if (!pci_present() || offset >= PCI_CONFIG_SPACE_SIZE) {
        return 0xFFFFFFFFU;
    }
    return pci_io_read32(pci_config_address(bus, slot, func, (uint8_t)offset));
}

uint32_t pci_config_address(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return PCI_CONFIG_ENABLE_BIT |
        ((uint32_t)bus << 16) |
        ((uint32_t)slot << 11) |
        ((uint32_t)func << 8) |
        (offset & 0xFCU);
}

uint8_t pci_read_config8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t word = pci_read_aligned_config32(bus, slot, func, (uint16_t)(offset & 0xFCU));
    return (uint8_t)((word >> ((offset & 0x3U) * 8U)) & 0xFFU);
}

uint16_t pci_read_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint16_t aligned = (uint16_t)(offset & 0xFCU);
    uint32_t shift = (uint32_t)(offset & 0x3U) * 8U;
    uint64_t window = pci_read_aligned_config32(bus, slot, func, aligned);

    if ((offset & 0x3U) == 0x3U) {
        window |= (uint64_t)pci_read_aligned_config32(bus, slot, func, aligned + 4U) << 32;
    }

    return (uint16_t)((window >> shift) & 0xFFFFU);
}

uint32_t pci_read_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return pci_read_aligned_config32(bus, slot, func, (uint16_t)(offset & 0xFCU));
}

void pci_write_config8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t val) {
    uint16_t aligned = (uint16_t)(offset & 0xFCU);
    uint32_t shift = (uint32_t)(offset & 0x3U) * 8U;
    uint32_t word = pci_read_aligned_config32(bus, slot, func, aligned);

    if (!pci_present()) {
        return;
    }

    word &= ~(0xFFU << shift);
    word |= (uint32_t)val << shift;
    pci_io_write32(pci_config_address(bus, slot, func, (uint8_t)aligned), word);
}

void pci_write_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t val) {
    uint16_t aligned = (uint16_t)(offset & 0xFCU);
    uint32_t byte_index = offset & 0x3U;

    if (!pci_present()) {
        return;
    }

    if (byte_index == 0x3U) {
        pci_write_config8(bus, slot, func, offset, (uint8_t)(val & 0xFFU));
        if (aligned + 4U < PCI_CONFIG_SPACE_SIZE) {
            pci_write_config8(bus, slot, func, (uint8_t)(offset + 1U), (uint8_t)(val >> 8));
        }
        return;
    }

    uint32_t shift = byte_index * 8U;
    uint32_t word = pci_read_aligned_config32(bus, slot, func, aligned);

    word &= ~(0xFFFFU << shift);
    word |= (uint32_t)val << shift;
    pci_io_write32(pci_config_address(bus, slot, func, (uint8_t)aligned), word);
}

void pci_write_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    if (!pci_present()) {
        return;
    }
    pci_io_write32(pci_config_address(bus, slot, func, offset), val);
}

uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return pci_read_config16(bus, slot, func, offset);
}

void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    pci_write_config32(bus, slot, func, offset, val);
}

void pci_check_device(uint8_t bus, uint8_t device) {
    uint8_t function = 0;
 
    uint32_t vendorID = pci_read_config16(bus, device, function, 0);
    if(vendorID == 0xFFFF) return; // Device doesn't exist

    uint32_t deviceID = pci_read_config16(bus, device, function, 2);
    // Lower 8 bits of 0x0A is sub-class, 0x0B is class
    uint32_t classCode = pci_read_config16(bus, device, function, 0x0A);
    
    char buf[64];
    sprintf(buf, "PCI %02x:%02x.0 %04x:%04x [%04x]\n", 
            bus, device, vendorID, deviceID, classCode);
    kprint(buf);

    pci_device_t *dev = (pci_device_t *)kmalloc(sizeof(pci_device_t));
    if (dev) {
        dev->bus = bus;
        dev->slot = device;
        dev->func = function;
        dev->vendor_id = (uint16_t)vendorID;
        dev->device_id = (uint16_t)deviceID;
        dev->class_code = classCode;
        dev->next = NULL;

        if (pci_devices_tail) {
            pci_devices_tail->next = dev;
        } else {
            pci_devices_head = dev;
        }
        pci_devices_tail = dev;
    }
}

void pci_scan(void) {
    if (!pci_present()) {
        return;
    }

    for(uint16_t bus = 0; bus < 256; bus++) {
        for(uint8_t device = 0; device < 32; device++) {
            pci_check_device(bus, device);
        }
    }
}

pci_device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t *from) {
    pci_device_t *curr = from ? from->next : pci_devices_head;
    while (curr) {
        if (curr->vendor_id == vendor_id && (device_id == 0xFFFF || curr->device_id == device_id)) {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

void pci_init(void) {
    if (!pci_present()) {
        kprint("PCI: configuration mechanism #1 unavailable, skipping scan.\n");
        return;
    }

    kprint("Scanning PCI Bus...\n");
    pci_scan();
}
