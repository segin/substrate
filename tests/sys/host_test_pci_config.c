#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t mock_config[256];
static uint32_t selected_address;

void kprint(const char *str) {
    (void)str;
}

void *kmalloc(size_t size) {
    return malloc(size);
}

void kfree(void *ptr) {
    free(ptr);
}

void pci_test_config_select(uint32_t address) {
    selected_address = address;
}

static int selected_matches_device(void) {
    return ((selected_address >> 16) & 0xFFU) == 2U &&
           ((selected_address >> 11) & 0x1FU) == 3U &&
           ((selected_address >> 8) & 0x7U) == 1U;
}

uint32_t pci_test_data_read32(void) {
    uint32_t value;
    uint8_t offset;

    if (!selected_matches_device()) {
        return 0xFFFFFFFFU;
    }

    offset = (uint8_t)(selected_address & 0xFCU);
    memcpy(&value, &mock_config[offset], sizeof(value));
    return value;
}

void pci_test_data_write32(uint32_t value) {
    uint8_t offset;

    if (!selected_matches_device()) {
        return;
    }

    offset = (uint8_t)(selected_address & 0xFCU);
    memcpy(&mock_config[offset], &value, sizeof(value));
}

#define HOST_TEST 1
#include "../../sys/arch/i386/pci.c"

static void reset_config(void) {
    memset(mock_config, 0xFF, sizeof(mock_config));
    selected_address = 0;
}

static void test_pci_config_address_aligns_offset(void) {
    assert(pci_config_address(2, 3, 1, 0x11) == 0x80021910U);
}

static void test_typed_reads_extract_expected_values(void) {
    uint32_t dword = 0x12345678U;

    reset_config();
    memcpy(&mock_config[0x10], &dword, sizeof(dword));

    assert(pci_read_config32(2, 3, 1, 0x10) == 0x12345678U);
    assert(pci_read_config16(2, 3, 1, 0x10) == 0x5678U);
    assert(pci_read_config16(2, 3, 1, 0x12) == 0x1234U);
    assert(pci_read_config8(2, 3, 1, 0x13) == 0x12U);
}

static void test_word_reads_cross_dword_boundary(void) {
    uint32_t low = 0xAAFFFFFFU;
    uint32_t high = 0xFFFFFFBBU;

    reset_config();
    memcpy(&mock_config[0x0C], &low, sizeof(low));
    memcpy(&mock_config[0x10], &high, sizeof(high));

    assert(pci_read_config16(2, 3, 1, 0x0F) == 0xBBAAU);
}

static void test_missing_device_reads_as_all_ones(void) {
    reset_config();
    assert(pci_read_config8(0, 0, 0, 0) == 0xFFU);
    assert(pci_read_config16(0, 0, 0, 0) == 0xFFFFU);
    assert(pci_read_config32(0, 0, 0, 0) == 0xFFFFFFFFU);
}

int main(void) {
    test_pci_config_address_aligns_offset();
    test_typed_reads_extract_expected_values();
    test_word_reads_cross_dword_boundary();
    test_missing_device_reads_as_all_ones();
    puts("host_test_pci_config: PASS");
    return 0;
}
