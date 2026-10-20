#ifndef _PCI_H
#define _PCI_H

#include <kern/pci.h>

#define PCI_CONFIG_ADDRESS PCI_CONFIG_ADDRESS_PORT
#define PCI_CONFIG_DATA    PCI_CONFIG_DATA_PORT

#ifdef HOST_TEST
/* Config-space port I/O, supplied by the host test that builds pci.c. */
void pci_test_config_select(uint32_t address);
uint32_t pci_test_config_readback(void);
uint32_t pci_test_data_read32(void);
void pci_test_data_write32(uint32_t value);
#endif

#endif
