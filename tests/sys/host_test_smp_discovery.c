#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define HOST_TEST 1

#include <arch/i386/pmap.h>
#include <arch/x86-common/ioapic.h>
#include <arch/x86-common/lapic.h>

static uint8_t bios_mem[0x100000];
static struct pmap mock_kernel_pmap;
static uint32_t mock_lapic_base;
static int mock_ioapic_count;
static uintptr_t mock_ioapic_base;
static uint8_t mock_ioapic_id;
static uint32_t mock_ioapic_gsi_base;

void early_uart_print(const char *msg) {
    (void)msg;
}

void kprint(const char *msg) {
    (void)msg;
}

pmap_t pmap_kernel(void) {
    return &mock_kernel_pmap;
}

void lapic_set_base(uint32_t phys_addr) {
    mock_lapic_base = phys_addr;
}

int ioapic_register(uintptr_t base, uint8_t id, uint32_t gsi_base) {
    mock_ioapic_count++;
    mock_ioapic_base = base;
    mock_ioapic_id = id;
    mock_ioapic_gsi_base = gsi_base;
    return 0;
}

void ioapic_register_isa_override(uint8_t bus, uint8_t source_irq, uint32_t gsi, uint16_t flags) {
    (void)bus;
    (void)source_irq;
    (void)gsi;
    (void)flags;
}

static void *test_mapper(uint32_t phys, uint32_t map_limit) {
    if (phys >= map_limit || phys >= sizeof(bios_mem)) {
        return NULL;
    }
    return &bios_mem[phys];
}

#include "../../sys/arch/i386/smp_discovery.c"

static uint8_t checksum8(const void *ptr, size_t len) {
    const uint8_t *bytes = (const uint8_t *)ptr;
    uint8_t sum = 0;

    for (size_t i = 0; i < len; i++) {
        sum = (uint8_t)(sum + bytes[i]);
    }

    return sum;
}

static void finalize_checksum(void *ptr, size_t len, size_t checksum_offset) {
    uint8_t *bytes = (uint8_t *)ptr;

    bytes[checksum_offset] = 0;
    bytes[checksum_offset] = (uint8_t)(0u - checksum8(ptr, len));
}

static void reset_state(void) {
    memset(bios_mem, 0, sizeof(bios_mem));
    memset(cpus, 0, sizeof(cpus));
    memset(&mock_kernel_pmap, 0, sizeof(mock_kernel_pmap));
    cpu_count = 1;
    smp_mp_config_phys = 0;
    mock_lapic_base = 0;
    mock_ioapic_count = 0;
    mock_ioapic_base = 0;
    mock_ioapic_id = 0;
    mock_ioapic_gsi_base = 0;
}

static void populate_mp_tables(uint32_t mpfp_phys, uint32_t mpc_phys) {
    struct mp_floating_ptr *mpfp = (struct mp_floating_ptr *)&bios_mem[mpfp_phys];
    struct mp_config_table *mpc = (struct mp_config_table *)&bios_mem[mpc_phys];
    struct mp_processor_entry *cpu0;
    struct mp_processor_entry *cpu1;
    struct mp_ioapic_entry *ioapic;

    memset(mpfp, 0, sizeof(*mpfp));
    memcpy(mpfp->signature, "_MP_", 4);
    mpfp->config_table = mpc_phys;
    mpfp->length = 1;
    mpfp->spec_rev = 4;
    finalize_checksum(mpfp, 16, offsetof(struct mp_floating_ptr, checksum));

    memset(mpc, 0, sizeof(*mpc));
    memcpy(mpc->signature, "PCMP", 4);
    memcpy(mpc->oem_id, "SUBSTRAT", 8);
    memcpy(mpc->product_id, "HOSTTESTMP  ", 12);
    mpc->spec_rev = 4;
    mpc->entry_count = 3;
    mpc->lapic_addr = 0xFEE00000u;
    mpc->base_length = sizeof(*mpc) + sizeof(*cpu0) + sizeof(*cpu1) + sizeof(*ioapic);

    cpu0 = (struct mp_processor_entry *)(mpc + 1);
    memset(cpu0, 0, sizeof(*cpu0));
    cpu0->type = 0;
    cpu0->local_apic_id = 0;
    cpu0->local_apic_version = 0x14;
    cpu0->cpu_flags = MP_PROCESSOR_ENABLED | MP_PROCESSOR_BSP;

    cpu1 = cpu0 + 1;
    memset(cpu1, 0, sizeof(*cpu1));
    cpu1->type = 0;
    cpu1->local_apic_id = 1;
    cpu1->local_apic_version = 0x14;
    cpu1->cpu_flags = MP_PROCESSOR_ENABLED;

    ioapic = (struct mp_ioapic_entry *)(cpu1 + 1);
    memset(ioapic, 0, sizeof(*ioapic));
    ioapic->type = 2;
    ioapic->ioapic_id = 2;
    ioapic->ioapic_version = 0x11;
    ioapic->ioapic_flags = MP_IOAPIC_ENABLED;
    ioapic->ioapic_addr = 0xFEC00000u;

    finalize_checksum(mpc, mpc->base_length, offsetof(struct mp_config_table, checksum));
}

static void test_mp_tables_from_ebda(void) {
    uint16_t *ebda_seg = (uint16_t *)&bios_mem[0x40e];
    uint32_t ebda_phys = 0x0009FC00u;
    uint32_t mpc_phys = 0x0009FC10u;

    reset_state();
    *ebda_seg = (uint16_t)(ebda_phys >> 4);
    populate_mp_tables(ebda_phys, mpc_phys);

    assert(smp_try_mp_tables(0, EARLY_DIRECTMAP_LIMIT, test_mapper));
    assert(cpu_count == 2);
    assert(cpus[0].lapic_id == 0);
    assert(cpus[1].lapic_id == 1);
    assert(mock_lapic_base == 0xFEE00000u);
    assert(mock_ioapic_count == 0);
    assert(smp_mp_config_phys == mpc_phys);
}

static void test_mp_tables_cached_after_page_zero_removed(void) {
    uint16_t *ebda_seg = (uint16_t *)&bios_mem[0x40e];
    uint32_t ebda_phys = 0x0009FC00u;
    uint32_t mpc_phys = 0x0009FC10u;

    reset_state();
    *ebda_seg = (uint16_t)(ebda_phys >> 4);
    populate_mp_tables(ebda_phys, mpc_phys);

    assert(smp_try_mp_tables(0, EARLY_DIRECTMAP_LIMIT, test_mapper));

    memset(cpus, 0, sizeof(cpus));
    cpu_count = 1;
    mock_ioapic_count = 0;

    assert(smp_try_mp_tables(0, FULL_DIRECTMAP_LIMIT, test_mapper));
    assert(cpu_count == 2);
    assert(cpus[0].lapic_id == 0);
    assert(cpus[1].lapic_id == 1);
    assert(mock_ioapic_count == 1);
    assert(mock_ioapic_base == 0xFEC00000u);
    assert(mock_ioapic_id == 2);
    assert(mock_ioapic_gsi_base == 0);
}

int main(void) {
    test_mp_tables_from_ebda();
    test_mp_tables_cached_after_page_zero_removed();
    puts("PASS: MP-table discovery fallback");
    return 0;
}
