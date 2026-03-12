#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t mock_config[256];
static uint32_t selected_address;
static int mock_pci_present = 1;

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

uint32_t pci_test_config_readback(void) {
    return mock_pci_present ? selected_address : 0U;
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
    mock_pci_present = 1;
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

static void test_absent_pci_bus_reads_as_all_ones_and_ignores_writes(void) {
    uint32_t before = 0x11223344U;

    reset_config();
    memcpy(&mock_config[0x20], &before, sizeof(before));
    mock_pci_present = 0;

    assert(!pci_present());
    assert(pci_read_config8(2, 3, 1, 0x20) == 0xFFU);
    assert(pci_read_config16(2, 3, 1, 0x20) == 0xFFFFU);
    assert(pci_read_config32(2, 3, 1, 0x20) == 0xFFFFFFFFU);

    pci_write_config32(2, 3, 1, 0x20, 0xAABBCCDDU);
    assert(memcmp(&mock_config[0x20], &before, sizeof(before)) == 0);
}

static void test_typed_writes_round_trip_and_preserve_neighbors(void) {
    uint32_t dword = 0x11223344U;

    reset_config();
    memcpy(&mock_config[0x20], &dword, sizeof(dword));

    pci_write_config8(2, 3, 1, 0x21, 0xAAU);
    assert(pci_read_config32(2, 3, 1, 0x20) == 0x1122AA44U);

    pci_write_config16(2, 3, 1, 0x22, 0xBCDEU);
    assert(pci_read_config32(2, 3, 1, 0x20) == 0xBCDEAA44U);

    pci_write_config32(2, 3, 1, 0x20, 0xCAFEBABEU);
    assert(pci_read_config32(2, 3, 1, 0x20) == 0xCAFEBABEU);
}

static void test_word_writes_cross_dword_boundary(void) {
    uint32_t low = 0x11223344U;
    uint32_t high = 0x55667788U;

    reset_config();
    memcpy(&mock_config[0x20], &low, sizeof(low));
    memcpy(&mock_config[0x24], &high, sizeof(high));

    pci_write_config16(2, 3, 1, 0x23, 0xA1B2U);

    assert(pci_read_config8(2, 3, 1, 0x23) == 0xB2U);
    assert(pci_read_config8(2, 3, 1, 0x24) == 0xA1U);
    assert(pci_read_config32(2, 3, 1, 0x20) == 0xB2223344U);
    assert(pci_read_config32(2, 3, 1, 0x24) == 0x556677A1U);
}

int main(void) {
    test_pci_config_address_aligns_offset();
    test_typed_reads_extract_expected_values();
    test_word_reads_cross_dword_boundary();
    test_missing_device_reads_as_all_ones();
    test_absent_pci_bus_reads_as_all_ones_and_ignores_writes();
    test_typed_writes_round_trip_and_preserve_neighbors();
    test_word_writes_cross_dword_boundary();
    puts("host_test_pci_config: PASS");
    return 0;
}
