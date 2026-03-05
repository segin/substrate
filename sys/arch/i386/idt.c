#include <arch/i386/idt.h>
#include <drivers/video/vga.h>
#include <arch/x86-common/io.h>
#include <string.h>
#include <stdio.h>
#include <drivers/input/keyboard.h>
#include <drivers/input/mouse.h>
#include <kern/console.h>
#include <kern/panic.h>
#include <arch/i386/fpu/fpu_emu.h>

#include <sys/proc.h>
#include <arch/i386/fpu/fpu_emu.h>

idt_entry_t idt_entries[256] __attribute__((aligned(16)));
idt_ptr_t   idt_ptr;

#include <kern/time.h>
#include <kern/sched.h>
#include <kern/cmdline.h>
#include <kern/debug.h>
#include <drivers/console/uart/uart.h>
#include <drivers/storage/ide/ide.h>
#include <arch/i386/vm86.h>
#include <arch/i386/pmap.h>
// isr externs are in idt.h now


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

#include <sys/random.h>

void isr_handler(registers_t *regs) {
    /* Harvest entropy from interrupt timing (TSC) */
    uint64_t tsc;
    __asm__ volatile("rdtsc" : "=A"(tsc));
    
    /* Mix TSC and interrupt info into pool (fast, no lock) */
    struct {
        uint64_t ts;
        uint32_t vector;
        uint32_t err;
    } __attribute__((packed)) entropy_data;
    
    entropy_data.ts = tsc;
    entropy_data.vector = regs->int_no;
    entropy_data.err = regs->err_code;
    
    random_harvest_fast(&entropy_data, sizeof(entropy_data));

    if (regs->int_no == 32) {
        timer_tick();
        
        /* Track user/system time for current process */
        if (current_process) {
            int is_usermode = (regs->cs & 0x3) == 3;
            rusage_add_tick(current_process, is_usermode);
        }
        
        if (regs->int_no >= 40) outb(0xA0, 0x20);
        outb(0x20, 0x20);
        sched_yield();
        signal_handle_pending(regs);
        return;
    }

    if (regs->int_no == 33) {
        keyboard_handler(regs);
    } else if (regs->int_no == 44) {
        mouse_handler(regs);
    } else if (regs->int_no == 7) {
        fpu_handler(regs);
    } else if (regs->int_no == 36) {
        uart_handler(regs);
    } else if (regs->int_no == 46 || regs->int_no == 47) {
        ide_irq_handler(regs->int_no == 47 ? 15 : 14);
    } else if (regs->int_no < 32) {
        // Exception - check if from user mode or kernel mode
        int is_usermode = (regs->cs & 0x3) == 3;
        
        // Fault Recovery (copyin/copyout safe handlers)
        // If a fault occurs in kernel mode while on_fault is set, resume there.
        if (!is_usermode && current_thread && current_thread->on_fault) {
             regs->eip = (uint32_t)current_thread->on_fault;
             current_thread->on_fault = 0; // Reset to avoid loop if handler faults
             return;
        }

        // VM86 Mode Check for GPF (13)
        if (regs->int_no == 13 && (regs->eflags & 0x20000)) { // EFLAGS_VM = 0x20000
            vm86_gpf_handler(regs);
            return;
        }

        uint32_t cr2 = 0;
        if (regs->int_no == 14) {
            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
            // COW and lazy fault handling are expected for normal process execution.
            if (pmap_fault(regs->err_code, cr2)) {
                return;
            }
        }

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
        
        /* TASKS.md L566: Invalid Opcode Decoding - dump instruction bytes at EIP */
        if (regs->int_no == 6) { /* Invalid Opcode */
            kprint("Instruction bytes at EIP: ");
            uint8_t *eip_ptr = (uint8_t *)regs->eip;
            /* Dump up to 16 bytes if address is valid */
            if (regs->eip >= 0xC0000000 || !is_usermode) {
                for (int i = 0; i < 16; i++) {
                    sprintf(buf, "%02X ", (unsigned int)eip_ptr[i]);
                    kprint(buf);
                }
            } else {
                kprint("(user address - cannot dump safely)");
            }
            kprint("\n");
        }
        if (regs->int_no == 14) {
            sprintf(buf, "CR2: 0x%08X\n", (unsigned int)cr2);
            kprint(buf);
        }
        
        if (is_usermode) {
            // User-mode crash - kill the process
            kprint("Killing user process.\n\n");
            if (current_process && current_process->pid == 1) {
                extern int cmdline_has(const char *key);
                
                // Dump memory if procmem argument present
                if (cmdline_has("procmem")) {
                    kprint("=== MEMORY SPACE DUMP (PID 1) ===\n");
                    pmap_dump(current_process->pmap);
                    kprint("=== END MEMORY DUMP ===\n\n");
                }
                
                debug_dump_processes();
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
    
    signal_handle_pending(regs);
}
