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
#include <arch/i386/gdt.h>
#include <sys/exec.h>
#include <arch/i386/percpu.h>
#include <exec/perso/personality.h>
// isr externs are in idt.h now

enum {
    IDT_VECTOR_COUNT = 256,
    IDT_EXCEPTION_BASE = 0,
    IDT_EXCEPTION_COUNT = 32,
    IDT_IRQ_BASE = 32,
    IDT_IRQ_COUNT = 16,
    IDT_SYSCALL_VECTOR = 0x80,
    KERNEL_CODE_SELECTOR = 0x08,
    IDT_FLAG_KERNEL_INT_GATE = IDT_FLAG_PRESENT | IDT_FLAG_INT32_GATE,

    PIC_MASTER_CMD = 0x20,
    PIC_MASTER_DATA = 0x21,
    PIC_SLAVE_CMD = 0xA0,
    PIC_SLAVE_DATA = 0xA1,
    PIC_EOI = 0x20,
    PIC_ICW1_INIT = 0x10,
    PIC_ICW1_ICW4 = 0x01,
    PIC_ICW4_8086 = 0x01,
    PIC_MASTER_REMAP_BASE = IDT_IRQ_BASE,
    PIC_SLAVE_REMAP_BASE = IDT_IRQ_BASE + 8,
    PIC_MASTER_CASCADE_IRQ = 0x04,
    PIC_SLAVE_CASCADE_ID = 0x02,

    CPU_EFLAGS_VM = 0x20000
};

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

static void pic_remap(void) {
    /*
     * Reprogram the legacy 8259 PIC so hardware IRQs land at 32..47 instead
     * of colliding with CPU exceptions at 0..31.
     */
    outb(PIC_MASTER_CMD, PIC_ICW1_INIT | PIC_ICW1_ICW4);
    outb(PIC_MASTER_DATA, PIC_MASTER_REMAP_BASE);
    outb(PIC_MASTER_DATA, PIC_MASTER_CASCADE_IRQ);
    outb(PIC_MASTER_DATA, PIC_ICW4_8086);
    outb(PIC_SLAVE_CMD, PIC_ICW1_INIT | PIC_ICW1_ICW4);
    outb(PIC_SLAVE_DATA, PIC_SLAVE_REMAP_BASE);
    outb(PIC_SLAVE_DATA, PIC_SLAVE_CASCADE_ID);
    outb(PIC_SLAVE_DATA, PIC_ICW4_8086);
    outb(PIC_MASTER_DATA, 0x00);
    outb(PIC_SLAVE_DATA, 0x00);
}

static void idt_install_range(uint8_t first_vector, const void *const *handlers,
                              size_t count, uint16_t selector, uint8_t flags) {
    size_t i;

    for (i = 0; i < count; i++) {
        idt_set_gate((uint8_t)(first_vector + i),
                     (uint32_t)(uintptr_t)handlers[i],
                     selector,
                     flags);
    }
}

void idt_init(void) {
    static const void *const exception_handlers[] = {
        isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7,
        isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15,
        isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
        isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
    };
    static const void *const irq_handlers[] = {
        isr32, isr33, isr34, isr35, isr36, isr37, isr38, isr39,
        isr40, isr41, isr42, isr43, isr44, isr45, isr46, isr47
    };

    /*
     * The IDT is a dense 256-entry table covering CPU exceptions, remapped
     * PIC IRQs, and user-callable software gates such as INT 0x80.
     */
    idt_ptr.limit = sizeof(idt_entry_t) * IDT_VECTOR_COUNT - 1;
    idt_ptr.base  = (uint32_t)&idt_entries;

    memset(&idt_entries, 0, sizeof(idt_entry_t) * IDT_VECTOR_COUNT);

    pic_remap();
    idt_install_range(IDT_EXCEPTION_BASE, exception_handlers,
                      IDT_EXCEPTION_COUNT, KERNEL_CODE_SELECTOR, IDT_FLAG_KERNEL_INT_GATE);
    idt_install_range(IDT_IRQ_BASE, irq_handlers,
                      IDT_IRQ_COUNT, KERNEL_CODE_SELECTOR, IDT_FLAG_KERNEL_INT_GATE);
    idt_set_gate(IDT_SYSCALL_VECTOR, (uint32_t)isr128,
                 KERNEL_CODE_SELECTOR, IDT_FLAG_USER_INT_GATE);

    idt_flush((uint32_t)&idt_ptr);
}

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt_entries[num].base_low = base & 0xFFFF;
    idt_entries[num].base_high = (base >> 16) & 0xFFFF;
    idt_entries[num].sel     = sel;
    idt_entries[num].always0 = 0;
    idt_entries[num].flags = flags;
}

#include <sys/random.h>

void isr_handler(registers_t *regs) {
    thread_t *cpu_thread = CURRENT_THREAD();
    current_thread = cpu_thread;
    current_process = cpu_thread ? cpu_thread->proc : NULL;

    /* Harvest entropy from interrupt timing (TSC) */
    uint64_t tsc;
    __asm__ volatile("rdtsc" : "=A"(tsc));
    int is_usermode = (regs->cs & 0x3) == 3;
    
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
    exec_maybe_unpin_current_thread(is_usermode);

    if (regs->int_no == IDT_IRQ_BASE) {
        timer_tick_context(is_usermode);
        
        /* Track user/system time for current process */
        if (current_process) {
            rusage_add_tick(current_process, is_usermode);
        }
        
        if (regs->int_no >= PIC_SLAVE_REMAP_BASE) outb(PIC_SLAVE_CMD, PIC_EOI);
        outb(PIC_MASTER_CMD, PIC_EOI);
        if (!current_thread || !(current_thread->flags & THREAD_F_NO_PREEMPT)) {
            sched_yield();
        }
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
        // Fault Recovery (copyin/copyout safe handlers)
        // If a fault occurs in kernel mode while on_fault is set, resume there.
        if (!is_usermode && current_thread && current_thread->on_fault) {
             regs->eip = (uint32_t)current_thread->on_fault;
             current_thread->on_fault = 0; // Reset to avoid loop if handler faults
             return;
        }

        // VM86 Mode Check for GPF (13)
        if (regs->int_no == 13 && (regs->eflags & CPU_EFLAGS_VM)) {
            vm86_gpf_handler(regs);
            return;
        }

        if (is_usermode && current_process) {
            struct personality *pers = perso_lookup(current_process->perso_id);

            if (pers && pers->handle_trap && pers->handle_trap(regs)) {
                signal_handle_pending(regs);
                return;
            }
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
            int sig = 0;
            int code = 0;
            uintptr_t addr = 0;

            if (current_process &&
                i386_trap_to_signal(regs, cr2, &sig, &code, &addr)) {
                if (current_thread && current_thread->proc == current_process) {
                    current_thread->trap_addr = addr;
                }
                trapsignal(current_process, sig, code);
                if (current_thread && current_thread->proc == current_process) {
                    current_thread->trap_addr = addr;
                }
                signal_handle_pending(regs);
                return;
            }

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
