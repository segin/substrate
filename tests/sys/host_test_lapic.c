#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>

// Mocking Kernel Headers
#define UNIT_TEST
#define LAPIC_ACCESS_OPS
#define PORT_IO_OPS

// Mock Console
void kprint(const char *s) {
    printf("%s", s);
}

// Mock Port I/O
void test_outb(uint16_t port, uint8_t val) {}
uint8_t test_inb(uint16_t port) { return 0; }

// Mock LAPIC MMIO Access
static uint32_t mock_lapic_regs[1024]; // 4KB space (indexes are byte offsets / 4)
static uint32_t mock_tccr_value = 0;

uint32_t test_lapic_read(uintptr_t base, uint32_t reg) {
    // printf("READ reg 0x%X\n", reg);
    if (reg == 0x390) { // LAPIC_TCCR
        // Simulate countdown
        if (mock_tccr_value > 0) mock_tccr_value--;
        return mock_tccr_value;
    }
    return mock_lapic_regs[reg / 4];
}

void test_lapic_write(uintptr_t base, uint32_t reg, uint32_t val) {
    // printf("WRITE reg 0x%X = 0x%X\n", reg, val);
    mock_lapic_regs[reg / 4] = val;
    if (reg == 0x380) { // LAPIC_TICR
        mock_tccr_value = val; // Reset current count on initial write
    }
}

// Include source file directly to test static functions and access internals
#include "../../sys/arch/x86-common/lapic.c"

// Helper to reset mocks
void reset_mocks() {
    memset(mock_lapic_regs, 0, sizeof(mock_lapic_regs));
    mock_tccr_value = 0;
    lapic_base = 0xFEE00000; // Set base to enable functions
    lapic_ticks_per_ms = 100; // Mock calibration: 100 ticks per ms
}

void test_delay_ms() {
    printf("Testing lapic_timer_delay_ms...\n");
    reset_mocks();

    // Call delay
    lapic_timer_delay_ms(10);

    // Verify TICR was set correctly
    // 10ms * 100 ticks/ms = 1000 ticks
    uint32_t expected_ticks = 1000;

    // Check if TICR (0x380) was written
    if (mock_lapic_regs[0x380 / 4] != expected_ticks) {
        printf("FAIL: Expected TICR = %u, got %u\n", expected_ticks, mock_lapic_regs[0x380 / 4]);
        return;
    }

    // Check if Timer Mode (0x320) was set to One-Shot | Masked
    // 0x320: LAPIC_TIMER
    // ONESHOT (0) | MASKED (0x10000) = 0x10000
    if (mock_lapic_regs[0x320 / 4] != 0x10000) {
        printf("FAIL: Expected TIMER mode = 0x10000, got 0x%X\n", mock_lapic_regs[0x320 / 4]);
        return;
    }

    // Check if Divider (0x3E0) was set to 16 (0x3)
    if (mock_lapic_regs[0x3E0 / 4] != 0x03) {
        printf("FAIL: Expected Divider = 0x03, got 0x%X\n", mock_lapic_regs[0x3E0 / 4]);
        return;
    }

    // Check if loop finished (TCCR reached 0)
    if (mock_tccr_value != 0) {
        printf("FAIL: Timer did not count down to 0\n");
        return;
    }

    printf("PASS: lapic_timer_delay_ms\n");
}

void test_delay_us() {
    printf("Testing lapic_timer_delay_us...\n");
    reset_mocks();

    // 200us * 100 ticks/ms / 1000 = 20 ticks
    lapic_timer_delay_us(200);

    uint32_t expected_ticks = 20;
    if (mock_lapic_regs[0x380 / 4] != expected_ticks) {
        printf("FAIL: Expected TICR = %u, got %u\n", expected_ticks, mock_lapic_regs[0x380 / 4]);
        return;
    }
    printf("PASS: lapic_timer_delay_us (200us -> 20 ticks)\n");

    reset_mocks();
    // Test small delay (rounding)
    // 5us * 100 / 1000 = 0.5 -> 0? Logic says at least 1.
    lapic_timer_delay_us(5);
    expected_ticks = 1; // Logic ensures >= 1 if us > 0
    if (mock_lapic_regs[0x380 / 4] != expected_ticks) {
        printf("FAIL: Expected TICR = %u for 5us, got %u\n", expected_ticks, mock_lapic_regs[0x380 / 4]);
        return;
    }
    printf("PASS: lapic_timer_delay_us (5us -> 1 tick)\n");
}

int main() {
    test_delay_ms();
    test_delay_us();
    return 0;
}
