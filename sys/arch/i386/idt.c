#include "idt.h"
#include "vga.h"
#include "io.h"
#include <string.h>
#include "../../drivers/input/keyboard.h"
#include "fpu/fpu_emu.h"

// Simple memset/memcpy if not available in freestanding headers yet
size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

void *memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    while(n--) *p++ = (unsigned char)c;
    return s;
}

idt_entry_t idt_entries[256];
idt_ptr_t   idt_ptr;

extern void idt_flush(uint32_t);

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
extern void isr32(void);  // IRQ0 (Timer)
extern void isr33(void);  // IRQ1 (Keyboard)
extern void isr128(void); // Syscall (0x80)

extern void timer_tick(void);
extern void sched_yield(void);
extern void signal_handle_pending(registers_t *regs);

void idt_init(void) {
    idt_ptr.limit = sizeof(idt_entry_t) * 256 - 1;
    idt_ptr.base  = (uint32_t)&idt_entries;

    memset(&idt_entries, 0, sizeof(idt_entry_t) * 256);

    // Remap the PIC (Programmable Interrupt Controller)
    // Master PIC
    outb(0x20, 0x11);
    outb(0x21, 0x20); // Offset 0x20 (32)
    outb(0x21, 0x04);
    outb(0x21, 0x01);

    // Slave PIC
    outb(0xA0, 0x11);
    outb(0xA1, 0x28); // Offset 0x28 (40)
    outb(0xA1, 0x02);
    outb(0xA1, 0x01);

    // Unmask IRQ0 (Timer) and IRQ1 (Keyboard)
    outb(0x21, 0xFC); // 11111100: Unmask bit 0 and 1
    outb(0xA1, 0xFF);

    // Initialize PIT (100Hz)
    uint32_t divisor = 1193180 / 100;
    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);

    // Set gates for exceptions
    idt_set_gate(0, (uint32_t)isr0, 0x08, 0x8E);
    idt_set_gate(1, (uint32_t)isr1, 0x08, 0x8E);
    idt_set_gate(2, (uint32_t)isr2, 0x08, 0x8E);
    idt_set_gate(3, (uint32_t)isr3, 0x08, 0x8E);
    idt_set_gate(4, (uint32_t)isr4, 0x08, 0x8E);
    idt_set_gate(5, (uint32_t)isr5, 0x08, 0x8E);
    idt_set_gate(6, (uint32_t)isr6, 0x08, 0x8E);
    idt_set_gate(7, (uint32_t)isr7, 0x08, 0x8E);
    idt_set_gate(8, (uint32_t)isr8, 0x08, 0x8E);
    idt_set_gate(9, (uint32_t)isr9, 0x08, 0x8E);
    idt_set_gate(10, (uint32_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint32_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint32_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint32_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint32_t)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint32_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint32_t)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint32_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint32_t)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint32_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint32_t)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint32_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint32_t)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint32_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint32_t)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint32_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint32_t)isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint32_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint32_t)isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint32_t)isr31, 0x08, 0x8E);
    
    // IRQ0 - Timer
    idt_set_gate(32, (uint32_t)isr32, 0x08, 0x8E);
    
    // IRQ1 - Keyboard
    idt_set_gate(33, (uint32_t)isr33, 0x08, 0x8E);
    
    // Syscall
    idt_set_gate(0x80, (uint32_t)isr128, 0x08, 0x8E);

    idt_flush((uint32_t)&idt_ptr);
}

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt_entries[num].base_low = base & 0xFFFF;
    idt_entries[num].base_high = (base >> 16) & 0xFFFF;
    idt_entries[num].sel     = sel;
    idt_entries[num].always0 = 0;
    idt_entries[num].flags   = flags;
}

#include "../../kern/panic.h"



static const char *exception_messages[] = {

    "Division By Zero",

    "Debug",

    "Non Maskable Interrupt",

    "Breakpoint",

    "Into Detected Overflow",

    "Out of Bounds",

    "Invalid Opcode",

    "No Coprocessor",

    "Double Fault",

    "Coprocessor Segment Overrun",

    "Bad TSS",

    "Segment Not Present",

    "Stack Fault",

    "General Protection Fault",

    "Page Fault",

    "Unknown Interrupt",

    "Coprocessor Fault",

    "Alignment Check",

    "Machine Check",

    "Reserved",

    "Reserved",

    "Reserved",

    "Reserved",

    "Reserved",

    "Reserved",

    "Reserved",

    "Reserved",

    "Reserved",

    "Reserved",

    "Reserved",

    "Security Exception",

    "Reserved"

};



void isr_handler(registers_t *regs) {



    if (regs->int_no == 32) {



        timer_tick();



        // Send EOI to PIC



        if (regs->int_no >= 40) outb(0xA0, 0x20);



        outb(0x20, 0x20);



        sched_yield();



        return;



    }







    if (regs->int_no == 33) {



        keyboard_handler(regs);



    } else if (regs->int_no == 7) {



        fpu_handler(regs);



    } else if (regs->int_no < 32) {



        vga_write("EXCEPTION: ", 11);



        vga_write(exception_messages[regs->int_no], strlen(exception_messages[regs->int_no]));



        vga_write("\n", 1);



        panic("Unhandled Exception");



    }







    // Send EOI to PIC for other IRQs



        if (regs->int_no >= 32 && regs->int_no <= 47) {



            if (regs->int_no >= 40) outb(0xA0, 0x20);



            outb(0x20, 0x20);



        }



    



        signal_handle_pending(regs);



    }



    




