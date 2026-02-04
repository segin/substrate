#ifndef _IDT_H
#define _IDT_H

#include <stdint.h>

// IDT Entry
struct idt_entry_struct {
    uint16_t base_low;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_high;
} __attribute__((packed));

typedef struct idt_entry_struct idt_entry_t;

// IDT Pointer
struct idt_ptr_struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

typedef struct idt_ptr_struct idt_ptr_t;

void idt_init(void);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);

// ISR Handlers (defined in isr.S)
extern void isr0(void);
extern void isr1(void);
// ... we can add more as needed, just 0-31 for exceptions usually

// Common handler called from ASM
typedef struct registers {
     uint32_t gs;                                    // Pushed second (lower address)
     uint32_t fs, es;                                // Pushed third and fourth
     uint32_t ds;                                    // Pushed first (higher address)
     uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; // Pushed by pusha
     uint32_t int_no, err_code;
     uint32_t eip, cs, eflags, useresp, ss; // Pushed by processor
} registers_t;

void isr_handler(registers_t *regs);
void syscall_handler(registers_t *regs);

#endif
