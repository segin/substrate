#include <arch/i386/pci.h>
#include <arch/x86-common/io.h>

typedef struct pci_ecam_window {
    uint8_t *base;
    uint16_t segment;
    uint8_t start_bus;
    uint8_t end_bus;
} pci_ecam_window_t;

static pci_ecam_window_t pci_ecam;
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

void pci_ecam_configure(void *base, uint16_t segment, uint8_t start_bus, uint8_t end_bus) {
    pci_ecam.base = (uint8_t *)base;
    pci_ecam.segment = segment;
    pci_ecam.start_bus = start_bus;
    pci_ecam.end_bus = end_bus;
}

void *pci_ecam_map(uint16_t segment, uint8_t bus, uint8_t slot, uint8_t func) {
    uintptr_t offset;

    if (pci_ecam.base == NULL || segment != pci_ecam.segment) {
        return NULL;
    }
    if (bus < pci_ecam.start_bus || bus > pci_ecam.end_bus || slot >= 32 || func >= 8) {
        return NULL;
    }

    offset = ((uintptr_t)(bus - pci_ecam.start_bus) << 20) |
             ((uintptr_t)slot << 15) |
             ((uintptr_t)func << 12);
    return pci_ecam.base + offset;
}

static uint32_t pci_read_aligned_config32(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset) {
    volatile uint32_t *ecam_reg;

    if (offset >= PCI_EXT_CONFIG_SPACE_SIZE) {
        return 0xFFFFFFFFU;
    }
    ecam_reg = (volatile uint32_t *)pci_ecam_map(0, bus, slot, func);
    if (ecam_reg != NULL) {
        return ecam_reg[offset >> 2];
    }
    if (!pci_present() || offset >= PCI_CONFIG_SPACE_SIZE) {
        return 0xFFFFFFFFU;
    }
    return pci_io_read32(pci_config_address(bus, slot, func, (uint8_t)offset));
}

uint32_t pci_config_address(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset) {
    return PCI_CONFIG_ENABLE_BIT |
        ((uint32_t)bus << 16) |
        ((uint32_t)slot << 11) |
        ((uint32_t)func << 8) |
        (offset & 0xFCU);
}

uint8_t pci_read_config8(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset) {
    uint32_t word = pci_read_aligned_config32(bus, slot, func, (uint16_t)(offset & (uint16_t)~0x3U));
    return (uint8_t)((word >> ((offset & 0x3U) * 8U)) & 0xFFU);
}

uint16_t pci_read_config16(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset) {
    uint16_t aligned = (uint16_t)(offset & (uint16_t)~0x3U);
    uint32_t shift = (uint32_t)(offset & 0x3U) * 8U;
    uint64_t window = pci_read_aligned_config32(bus, slot, func, aligned);

    if ((offset & 0x3U) == 0x3U) {
        window |= (uint64_t)pci_read_aligned_config32(bus, slot, func, aligned + 4U) << 32;
    }

    return (uint16_t)((window >> shift) & 0xFFFFU);
}

uint32_t pci_read_config32(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset) {
    return pci_read_aligned_config32(bus, slot, func, (uint16_t)(offset & (uint16_t)~0x3U));
}

void pci_write_config8(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset, uint8_t val) {
    uint16_t aligned = (uint16_t)(offset & (uint16_t)~0x3U);
    uint32_t shift = (uint32_t)(offset & 0x3U) * 8U;
    uint32_t word = pci_read_aligned_config32(bus, slot, func, aligned);

    if (pci_ecam_map(0, bus, slot, func) == NULL && !pci_present()) {
        return;
    }

    word &= ~(0xFFU << shift);
    word |= (uint32_t)val << shift;
    pci_write_config32(bus, slot, func, aligned, word);
}

void pci_write_config16(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset, uint16_t val) {
    uint16_t aligned = (uint16_t)(offset & (uint16_t)~0x3U);
    uint32_t byte_index = offset & 0x3U;

    if (pci_ecam_map(0, bus, slot, func) == NULL && !pci_present()) {
        return;
    }

    if (byte_index == 0x3U) {
        pci_write_config8(bus, slot, func, offset, (uint8_t)(val & 0xFFU));
        if (aligned + 4U < PCI_EXT_CONFIG_SPACE_SIZE) {
            pci_write_config8(bus, slot, func, (uint8_t)(offset + 1U), (uint8_t)(val >> 8));
        }
        return;
    }

    uint32_t shift = byte_index * 8U;
    uint32_t word = pci_read_aligned_config32(bus, slot, func, aligned);

    word &= ~(0xFFFFU << shift);
    word |= (uint32_t)val << shift;
    pci_write_config32(bus, slot, func, aligned, word);
}

void pci_write_config32(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset, uint32_t val) {
    volatile uint32_t *ecam_reg = (volatile uint32_t *)pci_ecam_map(0, bus, slot, func);

    if (ecam_reg != NULL && offset < PCI_EXT_CONFIG_SPACE_SIZE) {
        ecam_reg[offset >> 2] = val;
        return;
    }
    if (!pci_present()) {
        return;
    }
    pci_io_write32(pci_config_address(bus, slot, func, offset), val);
}

uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset) {
    return pci_read_config16(bus, slot, func, offset);
}

void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset, uint32_t val) {
    pci_write_config32(bus, slot, func, offset, val);
}
