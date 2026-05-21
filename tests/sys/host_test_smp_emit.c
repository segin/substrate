#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

#define HOST_TEST 1

// Mocks for smp_discovery.c dependencies
#include <arch/i386/pmap.h>
#include <arch/x86-common/ioapic.h>
#include <arch/x86-common/lapic.h>
#include <arch/i386/percpu.h>

void early_uart_print(const char *msg) { (void)msg; }
void kprint(const char *msg) { (void)msg; }
pmap_t pmap_kernel(void) { return NULL; }
void lapic_set_base(uint32_t phys_addr) { (void)phys_addr; }
int ioapic_register(uintptr_t base, uint8_t id, uint32_t gsi_base) { (void)base; (void)id; (void)gsi_base; return 0; }
void ioapic_register_isa_override(uint8_t bus, uint8_t source_irq, uint32_t gsi, uint16_t flags) { (void)bus; (void)source_irq; (void)gsi; (void)flags; }
uint32_t lapic_get_id(void) { return 0; }
void lapic_enable(uint8_t vector) { (void)vector; }
void percpu_init_cpu(int cpu) { (void)cpu; }
void gdt_init_cpu(int cpu) { (void)cpu; }
struct percpu_data *percpu_get_cpu(int cpu) { (void)cpu; return NULL; }
struct percpu_data *percpu_get(void) { return NULL; }
int percpu_get_cpu_id(void) { return 0; }
void lapic_send_init(uint8_t apic_id) { (void)apic_id; }
void lapic_timer_delay_ms(uint32_t ms) { (void)ms; }
void lapic_send_sipi(uint8_t apic_id, uint8_t vector) { (void)apic_id; (void)vector; }
void lapic_timer_delay_us(uint32_t us) { (void)us; }
int i386_cpu_has_apic(void) { return 1; }
int i386_cpu_has_cr4(void) { return 1; }

#include "../../sys/arch/i386/smp_discovery.c"

static char emit_buf[64];
static void mock_emit(const char *s) {
    strcat(emit_buf, s);
}

void test_smp_emit_u32(void) {
    struct {
        uint32_t val;
        const char *expected;
    } cases[] = {
        { 0, "0" },
        { 123, "123" },
        { 4294967295u, "4294967295" },
    };

    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        emit_buf[0] = '\0';
        smp_emit_u32(mock_emit, cases[i].val);
        if (strcmp(emit_buf, cases[i].expected) != 0) {
            fprintf(stderr, "Case %zu failed: expected %s, got %s\n", i, cases[i].expected, emit_buf);
            assert(0);
        }
    }
}

int main() {
    test_smp_emit_u32();
    printf("PASS: host_test_smp_emit\n");
    return 0;
}
