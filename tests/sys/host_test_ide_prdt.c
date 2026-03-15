#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <drivers/storage/ide/ide.h>

static void test_single_entry(void) {
    prdt_entry_t prdt[MAX_PRD_ENTRIES];
    int count;

    count = ide_prdt_build_entries(prdt, MAX_PRD_ENTRIES, 0x00102000U, 4096);
    assert(count == 1);
    assert(prdt[0].phys_addr == 0x00102000U);
    assert(prdt[0].byte_count == 4096);
    assert(prdt[0].eot == 1);
}

static void test_boundary_split(void) {
    prdt_entry_t prdt[MAX_PRD_ENTRIES];
    int count;

    count = ide_prdt_build_entries(prdt, MAX_PRD_ENTRIES, 0x0010FF00U, 4096);
    assert(count == 2);
    assert(prdt[0].phys_addr == 0x0010FF00U);
    assert(prdt[0].byte_count == 256);
    assert(prdt[0].eot == 0);
    assert(prdt[1].phys_addr == 0x00110000U);
    assert(prdt[1].byte_count == 3840);
    assert(prdt[1].eot == 1);
}

static void test_64k_encoding(void) {
    prdt_entry_t prdt[MAX_PRD_ENTRIES];
    int count;

    count = ide_prdt_build_entries(prdt, MAX_PRD_ENTRIES, 0x00200000U, 65536U);
    assert(count == 1);
    assert(prdt[0].byte_count == 0);
    assert(prdt[0].eot == 1);
}

static void test_entry_limit(void) {
    prdt_entry_t prdt[MAX_PRD_ENTRIES];
    int count;

    count = ide_prdt_build_entries(prdt, 1, 0x0010FF00U, 4096);
    assert(count == -1);
}

static void test_zero_length_rejected(void) {
    prdt_entry_t prdt[MAX_PRD_ENTRIES];
    int count;

    memset(prdt, 0xAA, sizeof(prdt));
    count = ide_prdt_build_entries(prdt, MAX_PRD_ENTRIES, 0x00200000U, 0);
    assert(count == -1);
}

int main(void) {
    test_single_entry();
    test_boundary_split();
    test_64k_encoding();
    test_entry_limit();
    test_zero_length_rejected();
    puts("host_test_ide_prdt: PASS");
    return 0;
}
