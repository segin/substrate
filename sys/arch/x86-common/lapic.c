#include <lapic.h>
#include <kern/console.h>

// Default LAPIC physical address (can be overridden by MADT)
#define LAPIC_DEFAULT_BASE  0xFEE00000

// Virtual address where LAPIC is mapped 
static uintptr_t lapic_base = 0;
static uint32_t lapic_phys_base = LAPIC_DEFAULT_BASE;
static bool lapic_initialized = false;

// Memory Access Abstraction (Overridable for Unit Tests)
#ifndef LAPIC_ACCESS_OPS
#define LAPIC_READ_MEM(base, reg)       (*((volatile uint32_t*)((base) + (reg))))
#define LAPIC_WRITE_MEM(base, reg, val) (*((volatile uint32_t*)((base) + (reg))) = (val))
#else
// For unit testing, these must be defined by the includer
extern uint32_t test_lapic_read(uintptr_t base, uint32_t reg);
extern void test_lapic_write(uintptr_t base, uint32_t reg, uint32_t val);
#define LAPIC_READ_MEM(base, reg)       test_lapic_read(base, reg)
#define LAPIC_WRITE_MEM(base, reg, val) test_lapic_write(base, reg, val)
#endif

// CPU Relax/Pause (Overridable)
#ifndef cpu_relax
#define cpu_relax() __asm__ volatile("pause")
#endif

// Read LAPIC register
static inline uint32_t lapic_read(uint32_t reg) {
    if (!lapic_base) return 0;
    return LAPIC_READ_MEM(lapic_base, reg);
}

// Write LAPIC register
static inline void lapic_write(uint32_t reg, uint32_t val) {
    if (!lapic_base) return;
    LAPIC_WRITE_MEM(lapic_base, reg, val);
}

// Set LAPIC base address (called from MADT parsing)
void lapic_set_base(uint32_t phys_addr) {
    lapic_phys_base = phys_addr;
}

// Get LAPIC base physical address
uint32_t lapic_get_base(void) {
    return lapic_phys_base;
}

void lapic_init(void) {
    kprint("LAPIC: Initializing at physical 0x");
    // Print hex (simple for now)
    char buf[9];
    uint32_t n = lapic_phys_base;
    for (int i = 7; i >= 0; i--) {
        int d = n & 0xF;
        buf[i] = (d < 10) ? ('0' + d) : ('A' + d - 10);
        n >>= 4;
    }
    buf[8] = '\0';
    kprint(buf);
    kprint("...\n");

    // 1. Map LAPIC MMIO region
    // The LAPIC is typically identity-mapped in early boot or needs explicit mapping.
    // For i386 with higher-half kernel, use direct physical access if identity mapped,
    // or map via pmap. For simplicity, assume identity-mapped in low 4GB (common for MMIO).
    // On x86, LAPIC at 0xFEE00000 is usually accessible directly.
    lapic_base = lapic_phys_base;  // Identity map assumption
    
    // Alternatively, map if needed:
    // extern pmap_t pmap_kernel(void);
    // pmap_enter(pmap_kernel(), lapic_phys_base, lapic_phys_base, 
    //            VM_PROT_READ | VM_PROT_WRITE, PTE_PCD);
    // lapic_base = lapic_phys_base;

    // 2. Verify LAPIC is present by reading version register
    uint32_t ver = lapic_read(LAPIC_VER);
    uint8_t version = ver & 0xFF;
    uint8_t max_lvt = ((ver >> 16) & 0xFF) + 1;
    
    kprint("LAPIC: Version 0x");
    buf[0] = (version >> 4) < 10 ? '0' + (version >> 4) : 'A' + (version >> 4) - 10;
    buf[1] = (version & 0xF) < 10 ? '0' + (version & 0xF) : 'A' + (version & 0xF) - 10;
    buf[2] = '\0';
    kprint(buf);
    kprint(", Max LVT entries: ");
    if (max_lvt >= 10) { kprint("1"); buf[0] = '0' + (max_lvt - 10); buf[1] = '\0'; }
    else { buf[0] = '0' + max_lvt; buf[1] = '\0'; }
    kprint(buf);
    kprint("\n");

    // 3. Enable LAPIC via SVR (will be done in separate task)
    // 4. Configure timer (separate task)
    // 5. Setup error handling (separate task)

    lapic_initialized = true;
    kprint("LAPIC: Initialized successfully.\n");
}

// Check if LAPIC is initialized
bool lapic_is_initialized(void) {
    return lapic_initialized;
}

// Enable LAPIC and set Spurious Interrupt Vector
void lapic_enable(uint8_t spurious_vector) {
    if (!lapic_base) return;
    
    // The spurious vector must have bits 0-3 set (vector 0xXF recommended)
    // Common choice is 0xFF
    if ((spurious_vector & 0x0F) != 0x0F) {
        kprint("LAPIC: Warning - spurious vector low nibble should be 0xF\n");
    }
    
    // Read current SVR
    uint32_t svr = lapic_read(LAPIC_SVR);
    
    // Set spurious vector and enable APIC
    svr &= ~0xFF;  // Clear vector bits
    svr |= spurious_vector;
    svr |= LAPIC_SVR_ENABLE;
    
    lapic_write(LAPIC_SVR, svr);
    
    kprint("LAPIC: Enabled with spurious vector 0x");
    char buf[3];
    buf[0] = (spurious_vector >> 4) < 10 ? '0' + (spurious_vector >> 4) : 'A' + (spurious_vector >> 4) - 10;
    buf[1] = (spurious_vector & 0xF) < 10 ? '0' + (spurious_vector & 0xF) : 'A' + (spurious_vector & 0xF) - 10;
    buf[2] = '\0';
    kprint(buf);
    kprint("\n");
}

// Disable LAPIC
void lapic_disable(void) {
    if (!lapic_base) return;
    
    uint32_t svr = lapic_read(LAPIC_SVR);
    svr &= ~LAPIC_SVR_ENABLE;
    lapic_write(LAPIC_SVR, svr);
    
    kprint("LAPIC: Disabled\n");
}

// ==================== LAPIC Timer ====================

// Timer calibration data
static uint32_t lapic_timer_frequency = 0;  // Hz
static uint32_t lapic_ticks_per_ms = 0;

// PIT frequency (standard 8254 PIT)
#define PIT_FREQUENCY   1193182

// I/O ports for PIT
#define PIT_CHANNEL0    0x40
#define PIT_CHANNEL2    0x42
#define PIT_COMMAND     0x43

// Port I/O Abstraction (Overridable for Unit Tests)
#ifndef PORT_IO_OPS
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
#else
extern void test_outb(uint16_t port, uint8_t val);
extern uint8_t test_inb(uint16_t port);
#define outb(port, val) test_outb(port, val)
#define inb(port)       test_inb(port)
#endif

// Calibrate LAPIC timer against PIT
// Returns: ticks per millisecond
uint32_t lapic_timer_calibrate(void) {
    if (!lapic_base) return 0;
    
    kprint("LAPIC: Calibrating timer against PIT...\n");
    
    // Set LAPIC timer divider to 16
    lapic_write(LAPIC_TDCR, LAPIC_TDCR_DIV16);
    
    // Configure PIT channel 2 for one-shot mode
    // Mode 0 (interrupt on terminal count), binary counting
    outb(PIT_COMMAND, 0xB0);  // Channel 2, lobyte/hibyte, mode 0
    
    // Set PIT to count down from ~10ms worth of ticks
    // 10ms = 1193182 / 100 = 11932 ticks
    uint16_t pit_count = 11932;
    outb(PIT_CHANNEL2, pit_count & 0xFF);
    outb(PIT_CHANNEL2, (pit_count >> 8) & 0xFF);
    
    // Set LAPIC timer to max count
    lapic_write(LAPIC_TICR, 0xFFFFFFFF);
    
    // Gate the PIT channel 2 (start counting)
    // Read port 0x61, set bit 0, clear bit 1
    uint8_t gate = inb(0x61);
    outb(0x61, (gate & ~0x02) | 0x01);
    
    // Wait for PIT to count down (poll bit 5 of port 0x61)
    while (!(inb(0x61) & 0x20)) {
        cpu_relax();
    }
    
    // Read LAPIC timer current count
    uint32_t lapic_end = lapic_read(LAPIC_TCCR);
    
    // Stop LAPIC timer
    lapic_write(LAPIC_TIMER, LAPIC_LVT_MASKED);
    
    // Calculate ticks elapsed
    uint32_t lapic_ticks = 0xFFFFFFFF - lapic_end;
    
    // Calculate frequency: ticks / 10ms = ticks * 100 per second
    lapic_timer_frequency = lapic_ticks * 100;
    lapic_ticks_per_ms = lapic_ticks / 10;
    
    kprint("LAPIC: Timer frequency ~");
    // Print MHz
    uint32_t mhz = lapic_timer_frequency / 1000000;
    char buf[16];
    int i = 0;
    if (mhz == 0) { buf[i++] = '0'; }
    else {
        char tmp[16]; int j = 0;
        while (mhz > 0) { tmp[j++] = '0' + (mhz % 10); mhz /= 10; }
        while (j > 0) { buf[i++] = tmp[--j]; }
    }
    buf[i] = '\0';
    kprint(buf);
    kprint(" MHz (");
    // Print ticks/ms
    i = 0;
    uint32_t tpm = lapic_ticks_per_ms;
    if (tpm == 0) { buf[i++] = '0'; }
    else {
        char tmp[16]; int j = 0;
        while (tpm > 0) { tmp[j++] = '0' + (tpm % 10); tpm /= 10; }
        while (j > 0) { buf[i++] = tmp[--j]; }
    }
    buf[i] = '\0';
    kprint(buf);
    kprint(" ticks/ms)\n");
    
    return lapic_ticks_per_ms;
}

// Set LAPIC timer divider
void lapic_timer_set_divider(uint8_t divider) {
    if (!lapic_base) return;
    lapic_write(LAPIC_TDCR, divider);
}

// Configure LAPIC timer in periodic mode
void lapic_timer_periodic(uint8_t vector, uint32_t ticks) {
    if (!lapic_base) return;
    
    // Set LVT Timer: periodic mode, vector
    lapic_write(LAPIC_TIMER, LAPIC_TIMER_PERIODIC | vector);
    
    // Set initial count
    lapic_write(LAPIC_TICR, ticks);
}

// Configure LAPIC timer in one-shot mode
void lapic_timer_oneshot(uint8_t vector, uint32_t ticks) {
    if (!lapic_base) return;
    
    // Set LVT Timer: one-shot mode, vector
    lapic_write(LAPIC_TIMER, LAPIC_TIMER_ONESHOT | vector);
    
    // Set initial count
    lapic_write(LAPIC_TICR, ticks);
}

// Stop LAPIC timer
void lapic_timer_stop(void) {
    if (!lapic_base) return;
    
    // Mask the timer
    lapic_write(LAPIC_TIMER, LAPIC_LVT_MASKED);
    lapic_write(LAPIC_TICR, 0);
}

// Get calibrated ticks per millisecond
uint32_t lapic_timer_ticks_per_ms(void) {
    return lapic_ticks_per_ms;
}

// Delay for specified milliseconds using LAPIC timer
void lapic_timer_delay_ms(uint32_t ms) {
    if (!lapic_base) return;

    if (lapic_ticks_per_ms == 0) {
        if (lapic_timer_calibrate() == 0) return;
    }

    uint32_t ticks = ms * lapic_ticks_per_ms;

    uint32_t old_timer = lapic_read(LAPIC_TIMER);
    uint32_t old_divide = lapic_read(LAPIC_TDCR);
    uint32_t old_ticr = lapic_read(LAPIC_TICR);

    lapic_write(LAPIC_TDCR, LAPIC_TDCR_DIV16);
    lapic_write(LAPIC_TIMER, LAPIC_TIMER_ONESHOT | LAPIC_LVT_MASKED);
    lapic_write(LAPIC_TICR, ticks);

    while (lapic_read(LAPIC_TCCR) > 0) {
        cpu_relax();
    }

    lapic_write(LAPIC_TDCR, old_divide);
    lapic_write(LAPIC_TIMER, old_timer);
    if (!(old_timer & LAPIC_LVT_MASKED)) {
        lapic_write(LAPIC_TICR, old_ticr);
    }
}

// Delay for specified microseconds using LAPIC timer
void lapic_timer_delay_us(uint32_t us) {
    if (!lapic_base) return;

    if (lapic_ticks_per_ms == 0) {
        if (lapic_timer_calibrate() == 0) return;
    }

    // Use 64-bit math to prevent overflow: (us * ticks_per_ms) / 1000
    uint64_t total_ticks = ((uint64_t)us * lapic_ticks_per_ms) / 1000;
    if (total_ticks == 0 && us > 0) total_ticks = 1; // Minimum 1 tick

    uint32_t old_timer = lapic_read(LAPIC_TIMER);
    uint32_t old_divide = lapic_read(LAPIC_TDCR);
    uint32_t old_ticr = lapic_read(LAPIC_TICR);

    lapic_write(LAPIC_TDCR, LAPIC_TDCR_DIV16);
    lapic_write(LAPIC_TIMER, LAPIC_TIMER_ONESHOT | LAPIC_LVT_MASKED);
    lapic_write(LAPIC_TICR, (uint32_t)total_ticks);

    while (lapic_read(LAPIC_TCCR) > 0) {
        cpu_relax();
    }

    lapic_write(LAPIC_TDCR, old_divide);
    lapic_write(LAPIC_TIMER, old_timer);
    if (!(old_timer & LAPIC_LVT_MASKED)) {
        lapic_write(LAPIC_TICR, old_ticr);
    }
}

// ==================== LAPIC Error Handling ====================

// Setup LAPIC error handling
void lapic_setup_error(uint8_t error_vector) {
    if (!lapic_base) return;
    
    // Clear any existing errors by writing to ESR (write triggers clear)
    lapic_write(LAPIC_ESR, 0);
    lapic_write(LAPIC_ESR, 0);  // Write twice per Intel specs
    
    // Configure LVT Error to deliver on specified vector
    lapic_write(LAPIC_ERROR, error_vector);
    
    kprint("LAPIC: Error handler configured on vector 0x");
    char buf[3];
    buf[0] = (error_vector >> 4) < 10 ? '0' + (error_vector >> 4) : 'A' + (error_vector >> 4) - 10;
    buf[1] = (error_vector & 0xF) < 10 ? '0' + (error_vector & 0xF) : 'A' + (error_vector & 0xF) - 10;
    buf[2] = '\0';
    kprint(buf);
    kprint("\n");
}

// Read and clear ESR (returns error codes)
uint32_t lapic_get_error(void) {
    if (!lapic_base) return 0;
    
    // Write to ESR to latch errors, then read
    lapic_write(LAPIC_ESR, 0);
    return lapic_read(LAPIC_ESR);
}

// Decode and print ESR error bits
void lapic_print_error(uint32_t esr) {
    kprint("LAPIC ESR: ");
    if (esr == 0) {
        kprint("No errors\n");
        return;
    }
    if (esr & 0x01) kprint("[Send Checksum Error] ");
    if (esr & 0x02) kprint("[Receive Checksum Error] ");
    if (esr & 0x04) kprint("[Send Accept Error] ");
    if (esr & 0x08) kprint("[Receive Accept Error] ");
    if (esr & 0x10) kprint("[Redirectable IPI] ");
    if (esr & 0x20) kprint("[Send Illegal Vector] ");
    if (esr & 0x40) kprint("[Receive Illegal Vector] ");
    if (esr & 0x80) kprint("[Illegal Register Address] ");
    kprint("\n");
}

void lapic_send_eoi(void) {
    lapic_write(LAPIC_EOI, 0);
}

uint32_t lapic_get_id(void) {
    return lapic_read(LAPIC_ID) >> 24;
}

// ==================== LAPIC IPI ====================

// Wait for ICR to be ready
static void lapic_ipi_wait(void) {
    while (lapic_read(LAPIC_ICRLO) & LAPIC_ICR_PENDING) {
        cpu_relax();
    }
}

// Send IPI to a specific CPU with full control
// dest_cpu: target LAPIC ID
// vector: interrupt vector (ignored for some delivery modes)
// delivery_mode: LAPIC_ICR_FIXED, LAPIC_ICR_INIT, LAPIC_ICR_SIPI, etc.
void lapic_send_ipi_ex(uint8_t dest_cpu, uint8_t vector, uint32_t delivery_mode) {
    if (!lapic_base) return;
    
    lapic_ipi_wait();
    
    // Set destination CPU in ICRHI
    lapic_write(LAPIC_ICRHI, ((uint32_t)dest_cpu) << 24);
    
    // Build ICR low: vector | delivery_mode | level assert
    uint32_t icr = vector | delivery_mode | LAPIC_ICR_ASSERT;
    lapic_write(LAPIC_ICRLO, icr);
}

// Send Fixed IPI to a specific CPU
void lapic_send_ipi(uint8_t dest_cpu, uint8_t vector) {
    lapic_send_ipi_ex(dest_cpu, vector, LAPIC_ICR_FIXED);
}

// Send Fixed IPI to all CPUs excluding self
void lapic_send_ipi_all_excl_self(uint8_t vector) {
    if (!lapic_base) return;
    
    lapic_ipi_wait();
    
    // All excluding self shorthand, fixed delivery
    uint32_t icr = vector | LAPIC_ICR_FIXED | LAPIC_ICR_ALL_EXCL_SELF | LAPIC_ICR_ASSERT;
    lapic_write(LAPIC_ICRLO, icr);
}

// Send INIT IPI to a specific CPU (for SMP bootstrap)
void lapic_send_init(uint8_t dest_cpu) {
    if (!lapic_base) return;
    
    lapic_ipi_wait();
    
    // Set destination
    lapic_write(LAPIC_ICRHI, ((uint32_t)dest_cpu) << 24);
    
    // INIT IPI: level assert
    uint32_t icr = LAPIC_ICR_INIT | LAPIC_ICR_LEVEL | LAPIC_ICR_ASSERT;
    lapic_write(LAPIC_ICRLO, icr);
    
    lapic_ipi_wait();
    
    // INIT de-assert (required for some CPUs)
    icr = LAPIC_ICR_INIT | LAPIC_ICR_LEVEL | LAPIC_ICR_DEASSERT;
    lapic_write(LAPIC_ICRLO, icr);
}

// Send Startup IPI (SIPI) to a specific CPU
// start_page: physical address >> 12 (must be < 1MB, hence 8-bit)
void lapic_send_sipi(uint8_t dest_cpu, uint8_t start_page) {
    if (!lapic_base) return;
    
    lapic_ipi_wait();
    
    // Set destination
    lapic_write(LAPIC_ICRHI, ((uint32_t)dest_cpu) << 24);
    
    // SIPI: vector = start page (physical addr >> 12)
    uint32_t icr = start_page | LAPIC_ICR_SIPI | LAPIC_ICR_ASSERT;
    lapic_write(LAPIC_ICRLO, icr);
}

// Send NMI to a specific CPU
void lapic_send_nmi(uint8_t dest_cpu) {
    if (!lapic_base) return;
    
    lapic_ipi_wait();
    
    lapic_write(LAPIC_ICRHI, ((uint32_t)dest_cpu) << 24);
    
    uint32_t icr = LAPIC_ICR_NMI | LAPIC_ICR_ASSERT;
    lapic_write(LAPIC_ICRLO, icr);
}

// Broadcast NMI to all CPUs excluding self
void lapic_send_nmi_all_excl_self(void) {
    if (!lapic_base) return;
    
    lapic_ipi_wait();
    
    uint32_t icr = LAPIC_ICR_NMI | LAPIC_ICR_ALL_EXCL_SELF | LAPIC_ICR_ASSERT;
    lapic_write(LAPIC_ICRLO, icr);
}
