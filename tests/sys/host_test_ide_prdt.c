#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <drivers/storage/ide/ide.h>
#include <drivers/storage/ide/ide_priv.h>
#include <arch/i386/pmap.h>

/*
 * ide_prdt_build_entries lives in ide_dma.c (compiled in by the Makefile
 * rule), which also pulls in the rest of the DMA path.  Those other
 * functions are never called from this test but still have to link, so
 * provide the shared state + leaf-helper stubs they reference.
 */
ide_channel_t ide_channels[MAX_IDE_CHANNELS];
prdt_entry_t ide_prdts[MAX_IDE_CHANNELS][MAX_PRD_ENTRIES];
volatile int ide_irq_complete[MAX_IDE_CHANNELS];
const char *const ide_channel_labels[MAX_IDE_CHANNELS] = {0};

int ide_debug_enabled(void) { return 0; }
void ide_bm_write8(uint8_t channel, uint8_t reg, uint8_t data) {
    (void)channel; (void)reg; (void)data;
}
uint8_t ide_bm_read8(uint8_t channel, uint8_t reg) {
    (void)channel; (void)reg; return 0;
}
void ide_bm_write32(uint8_t channel, uint8_t reg, uint32_t data) {
    (void)channel; (void)reg; (void)data;
}
uint8_t ide_read_reg(uint8_t channel, uint8_t reg) {
    (void)channel; (void)reg; return 0;
}
void ide_write_reg(uint8_t channel, uint8_t reg, uint8_t data) {
    (void)channel; (void)reg; (void)data;
}
void ide_select_drive(uint8_t channel, uint8_t drive) {
    (void)channel; (void)drive;
}
int ide_wait_ready(uint8_t channel, int timeout_ms, const char *op) {
    (void)channel; (void)timeout_ms; (void)op; return 0;
}
int ide_wait_ready_ex(uint8_t channel, int timeout_ms, const char *op,
                      int honor_err) {
    (void)channel; (void)timeout_ms; (void)op; (void)honor_err; return 0;
}
int ide_wait_irq_completion(uint8_t channel, uint32_t timeout_ms,
                            const char *op) {
    (void)channel; (void)timeout_ms; (void)op; return 0;
}
int kprintf(const char *fmt, ...) { (void)fmt; return 0; }
uintptr_t pmap_extract(pmap_t pmap, uintptr_t va) { (void)pmap; return va; }
pmap_t pmap_kernel(void) { return NULL; }

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

/*
 * [IDE-18] A region that runs off the top of the 32-bit physical space must
 * be rejected, not wrapped.
 *
 * The boundary used to be an absolute address, (phys & ~0xFFFF) + 0x10000,
 * which computes 0x100000000 for anything in the last 64 KiB and truncates
 * to 0.  Every "phys + size > boundary" test then passed, region_size became
 * 0 - phys, and -- worse -- the running phys_addr wrapped, so the SECOND
 * entry of this transfer described physical address 0.  A bus-master write
 * to PRD 0 lands on the real-mode IVT/BDA.
 */
static void test_four_gib_wrap_rejected(void) {
    prdt_entry_t prdt[MAX_PRD_ENTRIES];
    int count, i;

    memset(prdt, 0, sizeof(prdt));
    count = ide_prdt_build_entries(prdt, MAX_PRD_ENTRIES, 0xFFFFF000U, 8192);
    assert(count == -1);

    /* And nothing was left describing physical 0 with a live length. */
    for (i = 0; i < MAX_PRD_ENTRIES; i++)
        assert(!(prdt[i].phys_addr == 0 && prdt[i].byte_count != 0));
}

/* The last region ending exactly at the 4 GiB mark is legal: it does not
 * wrap into a following entry. */
static void test_ends_exactly_at_four_gib(void) {
    prdt_entry_t prdt[MAX_PRD_ENTRIES];
    int count;

    count = ide_prdt_build_entries(prdt, MAX_PRD_ENTRIES, 0xFFFFF000U, 4096);
    assert(count == 1);
    assert(prdt[0].phys_addr == 0xFFFFF000U);
    assert(prdt[0].byte_count == 4096);
    assert(prdt[0].eot == 1);
}

int main(void) {
    test_single_entry();
    test_boundary_split();
    test_64k_encoding();
    test_entry_limit();
    test_zero_length_rejected();
    test_four_gib_wrap_rejected();
    test_ends_exactly_at_four_gib();
    puts("host_test_ide_prdt: PASS");
    return 0;
}
