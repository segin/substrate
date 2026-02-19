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
// ISR Handlers (defined in isr.S)
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

// IRQs
extern void isr32(void);  // IRQ0 (Timer)
extern void isr33(void);  // IRQ1 (Keyboard)
extern void isr34(void);  // IRQ2 (Cascade)
extern void isr35(void);  // IRQ3 (COM2)
extern void isr36(void);  // IRQ4 (COM1)
extern void isr37(void);  // IRQ5 (LPT2)
extern void isr38(void);  // IRQ6 (Floppy)
extern void isr39(void);  // IRQ7 (LPT1/Spurious)
extern void isr40(void);  // IRQ8 (RTC)
extern void isr41(void);  // IRQ9
extern void isr42(void);  // IRQ10
extern void isr43(void);  // IRQ11
extern void isr44(void);  // IRQ12 (Mouse)
extern void isr45(void);  // IRQ13 (FPU)
extern void isr46(void);  // IRQ14 (IDE Primary)
extern void isr47(void);  // IRQ15 (IDE Secondary)

extern void isr128(void); // Syscall

void idt_flush(uint32_t);

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
void signal_handle_pending(registers_t *regs);

#endif
