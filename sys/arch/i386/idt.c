#include <arch/i386/idt.h>
#include <arch/i386/cpu.h>
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
#include <sys/preempt.h>
#include <arch/i386/fpu/fpu_emu.h>

idt_entry_t idt_entries[256] __attribute__((aligned(16)));
idt_ptr_t   idt_ptr;

#include <kern/time.h>
#include <kern/sched.h>
#include <kern/cmdline.h>
#include <kern/debug.h>
#include <sys/irq.h>
#include <drivers/console/uart/uart.h>
#include <drivers/storage/ide/ide.h>
#include <arch/i386/vm86.h>
#include <arch/i386/pmap.h>
#include <arch/i386/pmm.h>
#include <arch/i386/gdt.h>
#include <sys/exec.h>
#include <arch/i386/percpu.h>
#include <vm/vm_fault.h>
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

static uint32_t idt_read_cr2(void) {
#ifdef HOST_TEST
    extern uint32_t idt_host_cr2;
    return idt_host_cr2;
#else
    uint32_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    return cr2;
#endif
}

/*
 * Demand-paged user-stack grow-down.  On a not-present page fault
 * inside the current process's stack region [ustack_limit,
 * ustack_top), map one fresh zeroed RW page so the stack extends on
 * access.  exec only maps a small region at the top, so a process
 * costs just the stack it touches.  Returns 1 if serviced, 0 to let
 * the fault fall through (no process, fault outside the region, or
 * genuinely out of memory).
 */
static int vm_grow_user_stack(uint32_t cr2) {
#ifdef HOST_TEST
    (void)cr2;
    return 0;
#else
    process_t *p = current_process;
    if (!p || !p->pmap || p->ustack_top == 0) return 0;
    if (cr2 >= p->ustack_top || cr2 < p->ustack_limit) return 0;

    void *pa = pmm_alloc_block();
    if (!pa) return 0;                      /* genuine OOM */
    uint32_t pa_phys = (uint32_t)(uintptr_t)pa - 0xC0000000u;
    uintptr_t va = (uintptr_t)cr2 & ~0xFFFu;
    if (pmap_enter((pmap_t)p->pmap, va, pa_phys, VM_PROT_WRITE, 0) < 0) {
        pmm_free_block(pa);
        return 0;
    }
    memset(pa, 0, 0x1000);
    return 1;
#endif
}

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

    /* Panic IPI (0xFB).  Handler is a bare cli;hlt loop in isr.S —
     * intentionally bypasses the common stub since we never return. */
    {
        extern void isr_panic_ipi(void);
        idt_set_gate(0xFB, (uint32_t)(uintptr_t)isr_panic_ipi,
                     KERNEL_CODE_SELECTOR, IDT_FLAG_KERNEL_INT_GATE);
    }

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
    uint64_t tsc = i386_cpu_cycle_counter();
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
        
        (void)irq_dispatch(0, regs);
        if (regs->int_no >= PIC_SLAVE_REMAP_BASE) outb(PIC_SLAVE_CMD, PIC_EOI);
        outb(PIC_MASTER_CMD, PIC_EOI);
        if (current_thread && !(current_thread->flags & THREAD_F_NO_PREEMPT)) {
            /* Kernel preemption: preempt now when interrupted in user mode
             * (always safe) OR in kernel mode with no spinlock held
             * (preempt_count == 0).  A non-zero count means the interrupted
             * context is inside a critical section, so we only flag
             * needs_resched and let the matching release / a later tick
             * perform the switch. */
            if (is_usermode || preempt_count_get() == 0) {
                current_thread->needs_resched = 0;
                sched_yield();
            } else {
                current_thread->needs_resched = 1;
            }
        } else if (!current_thread && is_usermode) {
            sched_yield();
        }
        if (is_usermode) signal_handle_pending(regs);  /* not for kernel-mode IRQs */
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
    } else if (regs->int_no < 32) {
        // Exception handling

        // Page fault (14): try demand-paging BEFORE on_fault recovery so that
        // copyout/copyin on mmap'd anonymous pages (not yet faulted in) can
        // succeed via vm_fault instead of being short-circuited to EFAULT.
        uint32_t cr2 = 0;
        if (regs->int_no == 14) {
            cr2 = idt_read_cr2();
            // COW and lazy fault handling are expected for normal process execution.
            if (pmap_fault(regs->err_code, cr2)) {
                return;
            }

            /*
             * Recursive-fault breaker: if vm_fault keeps claiming SUCCESS at
             * the same address but the rep movsb keeps re-faulting (e.g.
             * because pmap_enter silently failed or the vm_object is
             * stale), the kernel hangs spinning on the same byte.  Track
             * the last faulting EIP+CR2 in this thread; on the third
             * consecutive matching fault, fall through to on_fault and
             * abandon vm_fault.
             */
            int loop_break = 0;
            if (!is_usermode && current_thread && current_thread->on_fault) {
                if (current_thread->fault_loop_eip == regs->eip &&
                    current_thread->fault_loop_cr2 == cr2) {
                    if (++current_thread->fault_loop_count >= 3) {
                        loop_break = 1;
                    }
                } else {
                    current_thread->fault_loop_eip = regs->eip;
                    current_thread->fault_loop_cr2 = cr2;
                    current_thread->fault_loop_count = 1;
                }
            }

            // Demand paging: try vm_fault for vm_map-backed regions.
            // This must work for BOTH usermode AND kernel mode (copyout/copyin)
            // so that syscalls like read/write can copy to/from demand-paged
            // user buffers backed by mmap(MAP_ANONYMOUS).
            if (!loop_break && current_process && current_process->vm_map) {
                uint8_t fault_prot = VM_PROT_READ;
                if (regs->err_code & 0x02) fault_prot |= VM_PROT_WRITE;
                if (regs->err_code & 0x10) fault_prot |= VM_PROT_EXEC;
                int vfr = vm_fault(current_process->vm_map, cr2, fault_prot);
                if (vfr == VM_FAULT_SUCCESS) {
                    return;
                }
                /* OOM (no free pages / no page-table page).  With strict
                 * commit accounting (vm_commit_charge at mmap/brk time)
                 * this is now a DEFENSIVE BACKSTOP: a correctly-charged
                 * page is always backable at fault time, so reaching here
                 * means either an uncharged path (COW/stack -- a documented
                 * follow-up) ran the system genuinely dry, or a true
                 * page-table allocation failure.  Substrate has no swap,
                 * so it is fatal to the process.  We deliver SIGSEGV
                 * (SEGV_MAPERR), NOT SIGBUS: SIGBUS for an anonymous-memory
                 * OOM matches no real OS (Linux/BSD raise SIGSEGV or invoke
                 * the OOM killer), and a portable program that installs a
                 * SIGSEGV handler should see it.  The kernel diagnostic is
                 * kept so the operator can tell a resource shortage from a
                 * userland-pointer bug.  trap_addr=cr2 names the page. */
                if (vfr == VM_FAULT_OOM && is_usermode && current_process) {
                    char oombuf[160];
                    snprintf(oombuf, sizeof(oombuf),
                        "VM: OOM at fault: pid=%d (%s) eip=0x%08X cr2=0x%08X "
                        "(no free page) -> SIGSEGV\n",
                        (int)current_process->pid,
                        current_process->comm[0] ? current_process->comm : "?",
                        (unsigned int)regs->eip,
                        (unsigned int)cr2);
                    kprint(oombuf);
                    if (current_thread && current_thread->proc == current_process) {
                        current_thread->trap_addr = cr2;
                    }
                    trapsignal(current_process, SIGSEGV, SEGV_MAPERR);
                    signal_handle_pending(regs);
                    return;
                }
            }

            /* Demand-paged user stack: a not-present fault inside the
             * process's stack region extends the stack one page down.
             * Runs after vm_fault so a real mmap region in range is
             * still serviced as a mapping, not mistaken for stack. */
            if (!(regs->err_code & 0x01) && vm_grow_user_stack(cr2)) {
                return;
            }
        }

        // Fault Recovery (copyin/copyout safe handlers)
        // If a fault occurs in kernel mode while on_fault is set, resume there.
        // This is checked AFTER page fault handling so demand-paging can service
        // the fault first; on_fault is the fallback for truly invalid accesses.
        if (!is_usermode && current_thread && current_thread->on_fault) {
             /* Reset the fault-loop counter — leaving on_fault is the
              * recovery boundary; whatever the next syscall does shouldn't
              * inherit our spin-detection state. */
             if (current_thread) {
                 current_thread->fault_loop_eip = 0;
                 current_thread->fault_loop_cr2 = 0;
                 current_thread->fault_loop_count = 0;
             }
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

        if (is_usermode) {
            int sig = 0;
            int code = 0;
            uintptr_t addr = 0;

            if (current_process &&
                i386_trap_to_signal(regs, cr2, &sig, &code, &addr)) {
                if (current_thread && current_thread->proc == current_process) {
                    current_thread->trap_addr = addr;
                }
                if (current_process->perso_id == PERS_ELKS) {
                    char elks_trapbuf[256];
                    snprintf(elks_trapbuf, sizeof(elks_trapbuf),
                            "TRAP[ELKS]: int=%u sig=%d code=%d addr=0x%08X eip=0x%08X cs=0x%04X ss=0x%04X esp=0x%08X ds=0x%04X\n",
                            (unsigned int)regs->int_no,
                            sig,
                            code,
                            (unsigned int)addr,
                            (unsigned int)regs->eip,
                            (unsigned int)regs->cs,
                            (unsigned int)regs->ss,
                            (unsigned int)regs->useresp,
                            (unsigned int)regs->ds);
                    kprint(elks_trapbuf);
                }
                /* Userspace-fault tracing.  Gated behind a simple
                 * `trap` kernel cmdline flag (not `debug=trap` — the
                 * comma-list debug parser was a foot-gun when this
                 * lookup mattered most).  Fires only on real fatal
                 * signals so the log volume is intrinsically low. */
                if (cmdline_has("trap") || cmdline_debug_enabled("trap")) {
                    char trapbuf[256];
                    snprintf(trapbuf, sizeof(trapbuf),
                            "TRAP: pid=%d (%s) exception %u -> signal %d code %d addr 0x%08X eip=0x%08X cs=0x%04X ss=0x%04X esp=0x%08X ds=0x%04X\n",
                            current_process ? (int)current_process->pid : -1,
                            (current_process && current_process->comm[0]) ? current_process->comm : "?",
                            (unsigned int)regs->int_no,
                            sig,
                            code,
                            (unsigned int)addr,
                            (unsigned int)regs->eip,
                            (unsigned int)regs->cs,
                            (unsigned int)regs->ss,
                            (unsigned int)regs->useresp,
                            (unsigned int)regs->ds);
                    kprint(trapbuf);

                    /* Walk the user-mode frame-pointer chain.  Requires
                     * the faulting binary was built with
                     * -fno-omit-frame-pointer (the substrate cross
                     * toolchain defaults are friendly here).  Bound
                     * the walk so a corrupt ebp can't loop forever,
                     * and copyin the words so a bad pointer faults
                     * us cleanly rather than double-faulting the
                     * kernel. */
                    {
                        extern int copyin(const void *src, void *dst, unsigned int size);
                        uint32_t fp = (uint32_t)regs->ebp;
                        kprint("TRAP: user backtrace:\n");
                        for (int depth = 0; depth < 16; depth++) {
                            char line[80];
                            uint32_t frame[2] = {0, 0};
                            if (fp == 0) {
                                kprint("  (end of chain)\n");
                                break;
                            }
                            if (fp & 3) {
                                snprintf(line, sizeof(line),
                                    "  <unaligned ebp=0x%08X>\n", fp);
                                kprint(line);
                                break;
                            }
                            if (copyin((const void *)(uintptr_t)fp,
                                       frame, sizeof(frame)) != 0) {
                                snprintf(line, sizeof(line),
                                    "  <unreadable ebp=0x%08X>\n", fp);
                                kprint(line);
                                break;
                            }
                            snprintf(line, sizeof(line),
                                "  #%d ebp=0x%08X ret=0x%08X\n",
                                depth, fp, frame[1]);
                            kprint(line);
                            if (frame[0] <= fp) break;  /* sane chain */
                            fp = frame[0];
                        }
                    }
                }
                trapsignal(current_process, sig, code);
                signal_handle_pending(regs);
                return;
            }
        }

        char buf[256];
        kprint("\nEXCEPTION: ");
        if (regs->int_no < 32) {
            kprint(exception_messages[regs->int_no]);
        } else {
            kprint("Unknown Exception");
        }
        if (is_usermode) {
            kprint(" (in user process)\n");
        } else {
            kprint(" (in kernel)\n");
        }
        snprintf(buf, sizeof(buf), "EIP: 0x%08X  CS: 0x%04X  ERR: 0x%08X\n", (unsigned int)regs->eip, (unsigned int)regs->cs, (unsigned int)regs->err_code);
        kprint(buf);
        snprintf(buf, sizeof(buf), "EAX: 0x%08X  EBX: 0x%08X  ECX: 0x%08X  EDX: 0x%08X\n", (unsigned int)regs->eax, (unsigned int)regs->ebx, (unsigned int)regs->ecx, (unsigned int)regs->edx);
        kprint(buf);
        snprintf(buf, sizeof(buf), "ESI: 0x%08X  EDI: 0x%08X  EBP: 0x%08X  ESP: 0x%08X\n", (unsigned int)regs->esi, (unsigned int)regs->edi, (unsigned int)regs->ebp, (unsigned int)regs->esp);
        kprint(buf);
        
        /* TASKS.md L566: Invalid Opcode Decoding - dump instruction bytes at EIP */
        if (regs->int_no == 6) { /* Invalid Opcode */
            kprint("Instruction bytes at EIP: ");
            uint8_t *eip_ptr = (uint8_t *)regs->eip;
            /* Dump up to 16 bytes if address is valid */
            if (regs->eip >= 0xC0000000 || !is_usermode) {
                for (int i = 0; i < 16; i++) {
                    snprintf(buf, sizeof(buf), "%02X ", (unsigned int)eip_ptr[i]);
                    kprint(buf);
                }
            } else {
                kprint("(user address - cannot dump safely)");
            }
            kprint("\n");
        }
        if (regs->int_no == 14) {
            snprintf(buf, sizeof(buf), "CR2: 0x%08X\n", (unsigned int)cr2);
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
            // Kernel-mode crash - panic with the trap frame so the user
            // sees regs/code/stack at the fault, not just at the panic().
            panic_with_regs("Unhandled Kernel Exception", regs);
        }
    }

    if (regs->int_no >= 32 && regs->int_no <= 47) {
        (void)irq_dispatch((unsigned int)(regs->int_no - IDT_IRQ_BASE), regs);
        if (regs->int_no >= 40) outb(0xA0, 0x20);
        outb(0x20, 0x20);
    }

    if (is_usermode) signal_handle_pending(regs);  /* not for kernel-mode IRQs */
}
