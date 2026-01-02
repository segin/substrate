#include "idt.h"
#include "../../drivers/video/vga.h"
#include "io.h"
#include <string.h>
#include <stdio.h>
#include "../../drivers/input/keyboard.h"
#include "../../drivers/input/mouse.h"
#include "../../kern/console.h"
#include "../../kern/panic.h"
#include "fpu/fpu_emu.h"

// Process/Thread access for exception handling
typedef enum { THREAD_READY, THREAD_RUNNING, THREAD_BLOCKED, THREAD_ZOMBIE } thread_state_t;
struct process;
struct thread { int tid; struct process *proc; void *kstack_ptr; void *kstack_top; void *instr_ptr; int priority; int base_priority; int sched_class; void *wait_chan; unsigned sig_pending; unsigned sig_mask; thread_state_t state; struct thread *next; };
struct process { int pid; int ppid; int exit_code; void *pers; void *fds[32]; void *root_node; };
extern struct thread *current_thread;
extern struct process *current_process;

idt_entry_t idt_entries[256] __attribute__((aligned(16)));
idt_ptr_t   idt_ptr;

extern void idt_flush(uint32_t);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);

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
extern void isr128(void); // Syscall (0x80)

extern void timer_tick(void);
extern void sched_yield(void);
extern void signal_handle_pending(registers_t *regs);

static const char *exception_messages[] = {
    "Division By Zero", "Debug", "Non Maskable Interrupt", "Breakpoint",
    "Into Detected Overflow", "Out of Bounds", "Invalid Opcode", "No Coprocessor",
    "Double Fault", "Coprocessor Segment Overrun", "Bad TSS", "Segment Not Present",
    "Stack Fault", "General Protection Fault", "Page Fault", "Unknown Interrupt",
    "Coprocessor Fault", "Alignment Check", "Machine Check",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Security Exception", "Reserved"
};

void idt_init(void) {
    idt_ptr.limit = sizeof(idt_entry_t) * 256 - 1;
    idt_ptr.base  = (uint32_t)&idt_entries;

    memset(&idt_entries, 0, sizeof(idt_entry_t) * 256);

    // Remap PIC
    outb(0x20, 0x11);
    outb(0x21, 0x20);
    outb(0x21, 0x04);
    outb(0x21, 0x01);
    outb(0xA0, 0x11);
    outb(0xA1, 0x28);
    outb(0xA1, 0x02);
    outb(0xA1, 0x01);
    outb(0x21, 0x00);
    outb(0xA1, 0x00);

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
    
    idt_set_gate(32, (uint32_t)isr32, 0x08, 0x8E);
    idt_set_gate(33, (uint32_t)isr33, 0x08, 0x8E);
    idt_set_gate(34, (uint32_t)isr34, 0x08, 0x8E);
    idt_set_gate(35, (uint32_t)isr35, 0x08, 0x8E);
    idt_set_gate(36, (uint32_t)isr36, 0x08, 0x8E);
    idt_set_gate(37, (uint32_t)isr37, 0x08, 0x8E);
    idt_set_gate(38, (uint32_t)isr38, 0x08, 0x8E);
    idt_set_gate(39, (uint32_t)isr39, 0x08, 0x8E);
    idt_set_gate(40, (uint32_t)isr40, 0x08, 0x8E);
    idt_set_gate(41, (uint32_t)isr41, 0x08, 0x8E);
    idt_set_gate(42, (uint32_t)isr42, 0x08, 0x8E);
    idt_set_gate(43, (uint32_t)isr43, 0x08, 0x8E);
    idt_set_gate(44, (uint32_t)isr44, 0x08, 0x8E);
    idt_set_gate(45, (uint32_t)isr45, 0x08, 0x8E);
    idt_set_gate(46, (uint32_t)isr46, 0x08, 0x8E);
    idt_set_gate(47, (uint32_t)isr47, 0x08, 0x8E);
    idt_set_gate(0x80, (uint32_t)isr128, 0x08, 0xEE); // DPL=3

    idt_flush((uint32_t)&idt_ptr);
}

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt_entries[num].base_low = base & 0xFFFF;
    idt_entries[num].base_high = (base >> 16) & 0xFFFF;
    idt_entries[num].sel     = sel;
    idt_entries[num].always0 = 0;
    idt_entries[num].flags   = flags;  // Use flags directly, caller sets DPL
    // So I will remove it.
    idt_entries[num].flags = flags;
}

void isr_handler(registers_t *regs) {
    if (regs->int_no == 32) {
        timer_tick();
        if (regs->int_no >= 40) outb(0xA0, 0x20);
        outb(0x20, 0x20);
        sched_yield();
        return;
    }

    if (regs->int_no == 33) {
        keyboard_handler(regs);
    } else if (regs->int_no == 44) {
        mouse_handler(regs);
    } else if (regs->int_no == 7) {
        fpu_handler(regs);
    } else if (regs->int_no < 32) {
        // Exception - check if from user mode or kernel mode
        int is_usermode = (regs->cs & 0x3) == 3;
        
        char buf[256];
        kprint("\nEXCEPTION: ");
        kprint(exception_messages[regs->int_no]);
        if (is_usermode) {
            kprint(" (in user process)\n");
        } else {
            kprint(" (in kernel)\n");
        }
        sprintf(buf, "EIP: 0x%08X  CS: 0x%04X  ERR: 0x%08X\n", (unsigned int)regs->eip, (unsigned int)regs->cs, (unsigned int)regs->err_code);
        kprint(buf);
        sprintf(buf, "EAX: 0x%08X  EBX: 0x%08X  ECX: 0x%08X  EDX: 0x%08X\n", (unsigned int)regs->eax, (unsigned int)regs->ebx, (unsigned int)regs->ecx, (unsigned int)regs->edx);
        kprint(buf);
        sprintf(buf, "ESI: 0x%08X  EDI: 0x%08X  EBP: 0x%08X  ESP: 0x%08X\n", (unsigned int)regs->esi, (unsigned int)regs->edi, (unsigned int)regs->ebp, (unsigned int)regs->esp);
        kprint(buf);
        
        if (is_usermode) {
            // User-mode crash - kill the process
            kprint("Killing user process.\n");
            if (current_process && current_process->pid == 1) {
                panic("init died - no recovery possible");
            }
            // Mark thread as zombie and yield
            if (current_thread) {
                current_thread->state = THREAD_ZOMBIE;
            }
            sched_yield();
            // Should not return, but if it does...
            for(;;) { __asm__ volatile("hlt"); }
        } else {
            // Kernel-mode crash - panic
            panic("Unhandled Kernel Exception");
        }
    }

    if (regs->int_no >= 32 && regs->int_no <= 47) {
        if (regs->int_no >= 40) outb(0xA0, 0x20);
        outb(0x20, 0x20);
    }
}
