#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <kern/pci.h>

static uint8_t ecam_space[2][32][8][4096];

void pci_test_config_select(uint32_t address) { (void)address; }
uint32_t pci_test_config_readback(void) { return PCI_CONFIG_ENABLE_BIT; }
uint32_t pci_test_data_read32(void) { return 0xFFFFFFFFU; }
void pci_test_data_write32(uint32_t value) { (void)value; }

#define HOST_TEST 1
#include "../../sys/arch/i386/pci.c"

int main(void) {
    memset(ecam_space, 0, sizeof(ecam_space));
    pci_ecam_configure(&ecam_space[0][0][0][0], 0, 0, 1);
    assert(pci_ecam_map(0, 0, 3, 2) == &ecam_space[0][3][2][0]);
    assert(pci_ecam_map(0, 1, 31, 7) == &ecam_space[1][31][7][0]);
    assert(pci_ecam_map(0, 2, 0, 0) == NULL);

    pci_write_config32(0, 0, 0, 0x100, 0xA5A5BEEFU);
    assert(pci_read_config32(0, 0, 0, 0x100) == 0xA5A5BEEFU);
    return 0;
}
