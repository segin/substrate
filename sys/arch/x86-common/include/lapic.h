#ifndef _ARCH_X86_LAPIC_H
#define _ARCH_X86_LAPIC_H

#include <stdint.h>
#include <stdbool.h>

// LAPIC Registers (offsets from base)
#define LAPIC_ID            0x0020  // Local APIC ID Register
#define LAPIC_VER           0x0030  // Local APIC Version Register
#define LAPIC_TPR           0x0080  // Task Priority Register
#define LAPIC_APR           0x0090  // Arbitration Priority Register
#define LAPIC_PPR           0x00A0  // Processor Priority Register
#define LAPIC_EOI           0x00B0  // End Of Interrupt Register
#define LAPIC_RRD           0x00C0  // Remote Read Register
#define LAPIC_LDR           0x00D0  // Logical Destination Register
#define LAPIC_DFR           0x00E0  // Destination Format Register
#define LAPIC_SVR           0x00F0  // Spurious Interrupt Vector Register
#define LAPIC_ISR           0x0100  // In-Service Register (8 registers)
#define LAPIC_TMR           0x0180  // Trigger Mode Register (8 registers)
#define LAPIC_IRR           0x0200  // Interrupt Request Register (8 registers)
#define LAPIC_ESR           0x0280  // Error Status Register
#define LAPIC_ICRLO         0x0300  // Interrupt Command Register (Low)
#define LAPIC_ICRHI         0x0310  // Interrupt Command Register (High)
#define LAPIC_TIMER         0x0320  // LVT Timer Register
#define LAPIC_THERMAL       0x0330  // LVT Thermal Sensor Register
#define LAPIC_PCINT         0x0340  // LVT Performance Counter Register
#define LAPIC_LINT0         0x0350  // LVT LINT0 Register
#define LAPIC_LINT1         0x0360  // LVT LINT1 Register
#define LAPIC_ERROR         0x0370  // LVT Error Register
#define LAPIC_TICR          0x0380  // Timer Initial Count Register
#define LAPIC_TCCR          0x0390  // Timer Current Count Register
#define LAPIC_TDCR          0x03E0  // Timer Divide Configuration Register

// SVR Flags
#define LAPIC_SVR_ENABLE    0x0100  // APIC Software Enable/Disable

// Timer Divide Values
#define LAPIC_TDCR_DIV1     0x0B    // Divide by 1
#define LAPIC_TDCR_DIV2     0x00    // Divide by 2
#define LAPIC_TDCR_DIV4     0x01    // Divide by 4
#define LAPIC_TDCR_DIV8     0x02    // Divide by 8
#define LAPIC_TDCR_DIV16    0x03    // Divide by 16
#define LAPIC_TDCR_DIV32    0x08    // Divide by 32
#define LAPIC_TDCR_DIV64    0x09    // Divide by 64
#define LAPIC_TDCR_DIV128   0x0A    // Divide by 128

// LVT Timer Modes
#define LAPIC_TIMER_ONESHOT     0x00000000
#define LAPIC_TIMER_PERIODIC    0x00020000
#define LAPIC_TIMER_TSC         0x00040000  // TSC-Deadline mode

// LVT Mask
#define LAPIC_LVT_MASKED        0x00010000

// ICR Delivery Modes
#define LAPIC_ICR_FIXED         0x00000000
#define LAPIC_ICR_LOWPRI        0x00000100
#define LAPIC_ICR_SMI           0x00000200
#define LAPIC_ICR_NMI           0x00000400
#define LAPIC_ICR_INIT          0x00000500
#define LAPIC_ICR_SIPI          0x00000600

// ICR Destination Shorthand
#define LAPIC_ICR_SELF          0x00040000
#define LAPIC_ICR_ALL           0x00080000
#define LAPIC_ICR_ALL_EXCL_SELF 0x000C0000

// ICR Level/Trigger
#define LAPIC_ICR_LEVEL         0x00008000
#define LAPIC_ICR_ASSERT        0x00004000
#define LAPIC_ICR_DEASSERT      0x00000000

// ICR Status
#define LAPIC_ICR_PENDING       0x00001000

// TLB Shootdown IPI vector (must not conflict with hardware IRQs)
#define TLB_SHOOTDOWN_VECTOR    0xFE

// Initialization and configuration
void lapic_init(void);
void lapic_set_base(uint32_t phys_addr);
uint32_t lapic_get_base(void);
bool lapic_is_initialized(void);

// Enable/Disable
void lapic_enable(uint8_t spurious_vector);
void lapic_disable(void);

// Timer
uint32_t lapic_timer_calibrate(void);
void lapic_timer_set_divider(uint8_t divider);
void lapic_timer_periodic(uint8_t vector, uint32_t ticks);
void lapic_timer_oneshot(uint8_t vector, uint32_t ticks);
void lapic_timer_stop(void);
uint32_t lapic_timer_ticks_per_ms(void);

// Timer Delay (Busy Wait)
void lapic_timer_delay_ms(uint32_t ms);
void lapic_timer_delay_us(uint32_t us);

// Error Handling
void lapic_setup_error(uint8_t error_vector);
uint32_t lapic_get_error(void);
void lapic_print_error(uint32_t esr);

// EOI
void lapic_send_eoi(void);

// ID
uint32_t lapic_get_id(void);

// IPI (Inter-Processor Interrupts)
void lapic_send_ipi(uint8_t dest_cpu, uint8_t vector);
void lapic_send_ipi_ex(uint8_t dest_cpu, uint8_t vector, uint32_t delivery_mode);
void lapic_send_ipi_all_excl_self(uint8_t vector);
void lapic_send_init(uint8_t dest_cpu);
void lapic_send_sipi(uint8_t dest_cpu, uint8_t start_page);
void lapic_send_nmi(uint8_t dest_cpu);
void lapic_send_nmi_all_excl_self(void);

#endif /* _ARCH_X86_LAPIC_H */
