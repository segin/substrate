#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>

#include <arch/x86-common/lapic.h>

// Mocking Kernel Headers
#define UNIT_TEST
#define LAPIC_ACCESS_OPS
#define PORT_IO_OPS

// Mock Console
void kprint(const char *s) {
    (void)s;
}

int i386_cpu_has_apic(void) {
    return 1;
}

// Mock Port I/O
static uint8_t mock_port61_value;
static int mock_port61_ready_after;
static int mock_port61_reads;

void test_outb(uint16_t port, uint8_t val) {
    if (port == 0x61) {
        mock_port61_value = val;
    }
}

uint8_t test_inb(uint16_t port) {
    if (port == 0x61) {
        if (mock_port61_reads >= mock_port61_ready_after) {
            return (uint8_t)(mock_port61_value | 0x20);
        }
        mock_port61_reads++;
        return mock_port61_value;
    }
    return 0;
}

// Mock LAPIC MMIO Access
static uint32_t mock_lapic_regs[1024]; // 4KB space (indexes are byte offsets / 4)
static uint32_t mock_tccr_value = 0;
static uint32_t mock_calibrated_elapsed = 0;
static uint32_t mock_icrlo_history[8];
static int mock_icrlo_writes;

uint32_t test_lapic_read(uintptr_t base, uint32_t reg) {
    (void)base;
    if (reg == 0x390) { // LAPIC_TCCR
        if (mock_calibrated_elapsed != 0) {
            return 0xFFFFFFFFu - mock_calibrated_elapsed;
        }
        // Simulate countdown
        if (mock_tccr_value > 0) mock_tccr_value--;
        return mock_tccr_value;
    }
    return mock_lapic_regs[reg / 4];
}

void test_lapic_write(uintptr_t base, uint32_t reg, uint32_t val) {
    (void)base;
    mock_lapic_regs[reg / 4] = val;
    if (reg == 0x380) { // LAPIC_TICR
        mock_tccr_value = val; // Reset current count on initial write
    }
    if (reg == LAPIC_ICRLO && mock_icrlo_writes < (int)(sizeof(mock_icrlo_history) / sizeof(mock_icrlo_history[0]))) {
        mock_icrlo_history[mock_icrlo_writes++] = val;
    }
}

// Include source file directly to test static functions and access internals
#include "../../sys/arch/x86-common/lapic.c"

// Helper to reset mocks
void reset_mocks() {
    memset(mock_lapic_regs, 0, sizeof(mock_lapic_regs));
    mock_tccr_value = 0;
    mock_calibrated_elapsed = 0;
    mock_port61_value = 0;
    mock_port61_ready_after = 0;
    mock_port61_reads = 0;
    memset(mock_icrlo_history, 0, sizeof(mock_icrlo_history));
    mock_icrlo_writes = 0;
    lapic_base = 0xFEE00000; // Set base to enable functions
    lapic_ticks_per_ms = 100; // Mock calibration: 100 ticks per ms
    lapic_initialized = false;
    mock_lapic_regs[LAPIC_TIMER / 4] = LAPIC_LVT_MASKED;
    mock_lapic_regs[LAPIC_TDCR / 4] = LAPIC_TDCR_DIV16;
}

void test_init_and_base(void) {
    reset_mocks();
    mock_lapic_regs[LAPIC_VER / 4] = (5u << 16) | 0x14u;

    lapic_set_base(0xFEE00000u);
    assert(lapic_get_base() == 0xFEE00000u);

    lapic_init();

    assert(lapic_is_initialized());
    assert(lapic_base == 0xFEE00000u);
}

void test_timer_calibration(void) {
    reset_mocks();
    mock_lapic_regs[LAPIC_VER / 4] = (5u << 16) | 0x14u;
    mock_calibrated_elapsed = 16000u;
    mock_port61_ready_after = 2;
    lapic_init();

    assert(lapic_timer_calibrate() == 1600u);
    assert(lapic_timer_ticks_per_ms() == 1600u);
    assert(mock_lapic_regs[LAPIC_TDCR / 4] == LAPIC_TDCR_DIV16);
    assert(mock_lapic_regs[LAPIC_TIMER / 4] == LAPIC_LVT_MASKED);
}

void test_ipi_encodings(void) {
    reset_mocks();
    lapic_send_ipi(3, 0x45);
    assert(mock_lapic_regs[LAPIC_ICRHI / 4] == (3u << 24));
    assert(mock_lapic_regs[LAPIC_ICRLO / 4] == (0x45u | LAPIC_ICR_FIXED | LAPIC_ICR_ASSERT));

    reset_mocks();
    lapic_send_ipi_ex(4, 0x46, LAPIC_ICR_LOWPRI);
    assert(mock_lapic_regs[LAPIC_ICRHI / 4] == (4u << 24));
    assert(mock_lapic_regs[LAPIC_ICRLO / 4] == (0x46u | LAPIC_ICR_LOWPRI | LAPIC_ICR_ASSERT));

    reset_mocks();
    lapic_send_nmi(5);
    assert(mock_lapic_regs[LAPIC_ICRHI / 4] == (5u << 24));
    assert(mock_lapic_regs[LAPIC_ICRLO / 4] == (LAPIC_ICR_NMI | LAPIC_ICR_ASSERT));

    reset_mocks();
    lapic_send_sipi(6, 0x08);
    assert(mock_lapic_regs[LAPIC_ICRHI / 4] == (6u << 24));
    assert(mock_lapic_regs[LAPIC_ICRLO / 4] == (0x08u | LAPIC_ICR_SIPI | LAPIC_ICR_ASSERT));

    reset_mocks();
    lapic_send_init(7);
    assert(mock_lapic_regs[LAPIC_ICRHI / 4] == (7u << 24));
    assert(mock_icrlo_writes >= 2);
    assert(mock_icrlo_history[0] == (LAPIC_ICR_INIT | LAPIC_ICR_LEVEL | LAPIC_ICR_ASSERT));
    assert(mock_icrlo_history[1] == (LAPIC_ICR_INIT | LAPIC_ICR_LEVEL | LAPIC_ICR_DEASSERT));
}

void test_delay_ms() {
    reset_mocks();

    // Call delay
    lapic_timer_delay_ms(10);

    // Verify TICR was set correctly
    // 10ms * 100 ticks/ms = 1000 ticks
    uint32_t expected_ticks = 1000;

    // Check if TICR (0x380) was written
    assert(mock_lapic_regs[LAPIC_TICR / 4] == expected_ticks);

    // Check if Timer Mode (0x320) was set to One-Shot | Masked
    // 0x320: LAPIC_TIMER
    // ONESHOT (0) | MASKED (0x10000) = 0x10000
    assert(mock_lapic_regs[LAPIC_TIMER / 4] == LAPIC_LVT_MASKED);

    // Check if Divider (0x3E0) was set to 16 (0x3)
    assert(mock_lapic_regs[LAPIC_TDCR / 4] == LAPIC_TDCR_DIV16);

    // Check if loop finished (TCCR reached 0)
    assert(mock_tccr_value == 0);
}

void test_delay_us() {
    reset_mocks();

    // 200us * 100 ticks/ms / 1000 = 20 ticks
    lapic_timer_delay_us(200);

    uint32_t expected_ticks = 20;
    assert(mock_lapic_regs[LAPIC_TICR / 4] == expected_ticks);

    reset_mocks();
    // Test small delay (rounding)
    // 5us * 100 / 1000 = 0.5 -> 0? Logic says at least 1.
    lapic_timer_delay_us(5);
    expected_ticks = 1; // Logic ensures >= 1 if us > 0
    assert(mock_lapic_regs[LAPIC_TICR / 4] == expected_ticks);
}

int main() {
    test_init_and_base();
    test_timer_calibration();
    test_ipi_encodings();
    test_delay_ms();
    test_delay_us();
    puts("PASS: host_test_lapic");
    return 0;
}
