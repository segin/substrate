#include <sys/types.h>
#include <sys/vm86.h>
#include <sys/sysarch.h>
#include <sys/copy.h>
#include <string.h>
#include <errno.h>
#include <kern/console.h>
#include <include/sys/proc.h>
#include <arch/i386/idt.h> 
#include <arch/i386/pmap.h>
#include <arch/i386/pmap.h>
#include "vm86.h" 

// Access to current process/thread
extern struct process *current_process;
extern struct thread *current_thread;

/*
 * VM86 Monitor Structure (TASKS.md L578)
 *
 * The V86 monitor manages the virtual machine state for real-mode emulation.
 * It tracks the virtual CPU state, provides interrupt reflection, and handles
 * I/O port virtualization.
 */
struct vm86_monitor {
    uint32_t vflags;        /* Virtual EFLAGS (includes VIF, VIP) */
    uint32_t pending_int;   /* Pending interrupt number, or -1 if none */
    int in_vm86;            /* Currently in VM86 mode */
    int in_kernel_bios;     /* Kernel-mode BIOS call active */
    int req_exit;           /* Request exit from loop */
    
    /* I/O port virtualization callbacks (optional, for advanced use) */
    void *io_context;       /* Opaque context for I/O handlers */
    
    /* Monitor communication (unhandled instructions signal monitor) */
    int signal_pending;     /* Signal monitor for unhandled opcode */
    uint32_t fault_eip;     /* EIP of faulting instruction */
    uint32_t fault_opcode;  /* Opcode that triggered fault */

    struct vm86_regs *out_regs; /* Output registers for kernel BIOS calls */
};

/* Per-process VM86 monitor (NULL if not using VM86) */
static struct vm86_monitor *current_vm86_monitor;
static uint8_t vm86_port_space[65536];

enum {
    VM86_ENTRY_EFLAGS = 0x20200U
};

#ifdef HOST_TEST
static uint8_t *vm86_test_memory;
static size_t vm86_test_memory_size;

void vm86_host_set_memory(void *base, size_t size) {
    vm86_test_memory = (uint8_t *)base;
    vm86_test_memory_size = size;
}
#endif

static uint8_t *vm86_linear_ptr(uint32_t linear, size_t size) {
#ifdef HOST_TEST
    if (!vm86_test_memory || linear > vm86_test_memory_size ||
        size > vm86_test_memory_size - linear) {
        return NULL;
    }
    return vm86_test_memory + linear;
#else
    (void)size;
    return (uint8_t *)(uintptr_t)linear;
#endif
}

static void vm86_prepare_entry(struct vm86_struct *info) {
    info->regs.eflags |= VM86_ENTRY_EFLAGS;
}

/*
 * vm86_monitor_init - Initialize VM86 monitor for current process
 *
 * Call before entering VM86 mode to set up virtualization state.
 */
void vm86_monitor_init(struct vm86_monitor *mon) {
    if (!mon) return;
    
    mon->vflags = 0x200;    /* IF set by default */
    mon->pending_int = (uint32_t)-1;
    mon->in_vm86 = 0;
    mon->in_kernel_bios = 0;
    mon->req_exit = 0;
    mon->io_context = NULL;
    mon->signal_pending = 0;
    mon->fault_eip = 0;
    mon->fault_opcode = 0;
    mon->out_regs = NULL;
    
    current_vm86_monitor = mon;
}

/*
 * vm86_monitor_get - Get current VM86 monitor
 */
struct vm86_monitor *vm86_monitor_get(void) {
    return current_vm86_monitor;
}

/*
 * vm86_monitor_signal_fault - Signal monitor that we have unhandled opcode
 */
void vm86_monitor_signal_fault(uint32_t eip, uint32_t opcode) {
    if (current_vm86_monitor) {
        current_vm86_monitor->signal_pending = 1;
        current_vm86_monitor->fault_eip = eip;
        current_vm86_monitor->fault_opcode = opcode;
    }
}
// Assembly helper to enter VM86 mode (defined in vm86_asm.S)
extern void vm86_enter(struct vm86_struct *info);

// Access to the syscall return registers (mapped in syscall.c)
/*
 * sys_vm86: Enter virtual 8086 mode.
 */
int sys_vm86(struct vm86_struct *info) {
    if (!info) return -EFAULT;
    
    struct vm86_struct k_info;
    if (copyin(info, &k_info, sizeof(k_info)) != 0) {
        return -EFAULT;
    }

    vm86_prepare_entry(&k_info);
    
    /* Call assembly helper to build stack frame and execute IRET */
    vm86_enter(&k_info);
    
    /*
     * We return here only when VM86 mode exits via vm86_bios_ret_point
     * (e.g. on HLT in kernel BIOS call).
     */
    return 0;
}

int vm86_init_bsd(void *args) {
    struct i386_vm86_args k_args;

    if (!args) return -EFAULT;
    if (copyin(args, &k_args, sizeof(k_args)) != 0) return -EFAULT;
    
    if (k_args.sub_op == VM86_INIT) {
        // BSD `init` usually enters the mode or prepares it.
        // If it implies entering, we need the struct.
        // Assuming sub_args points to `struct vm86_struct`.
        if (!k_args.sub_args) return -EINVAL;
        return sys_vm86((struct vm86_struct*)k_args.sub_args);
    }
    return -EINVAL;
}

void vm86_gpf_handler(registers_t *regs) {
    // We trapped here because of a sensitive instruction in VM86 mode (INT n, CLI, STI, IN/OUT)
    // We need to emulate it or reflect it to the monitor process.
    
    // 1. Fetch instruction byte(s) from CS:IP
    // Need linear address: (CS << 4) + EIP
    uint32_t cs = regs->cs & 0xFFFF;
    uint32_t ip = regs->eip & 0xFFFF;
    uint32_t linear = (cs << 4) + ip;
    
    uint8_t *code = vm86_linear_ptr(linear, 2);
    if (!code) {
        vm86_monitor_signal_fault(regs->eip, 0xFFFFFFFFU);
        return;
    }
    uint8_t opcode = code[0];
    
    // Valid IO/mem access check needed (skip for now, assuming permissive)
    
    if (opcode == 0xCD) { // INT n
        uint8_t int_no = code[1];
        // Emulate interrupt:
        // Push FLAGS, CS, IP to VM86 Stack
        // Jump to IVT[n]
        
        uint16_t ss = regs->ss;
        uint16_t sp = regs->useresp;
        uint32_t stack_linear = (ss << 4) + sp;
        uint16_t *stack = (uint16_t *)vm86_linear_ptr(stack_linear - 6U, 6);
        uint16_t *ivt;

        if (!stack) {
            vm86_monitor_signal_fault(regs->eip, opcode);
            return;
        }

        stack[0] = (uint16_t)(regs->eip + 2); /* Return after INT n */
        stack[1] = (uint16_t)regs->cs;
        stack[2] = (uint16_t)regs->eflags;
        
        regs->useresp = (uint32_t)(sp - 6);
        
        // Load new CS:IP from IVT (at 0x0000)
        ivt = (uint16_t *)vm86_linear_ptr(0, 4U * 256U);
        if (!ivt) {
            vm86_monitor_signal_fault(regs->eip, opcode);
            return;
        }
        regs->eip = ivt[int_no * 2];
        regs->cs  = ivt[int_no * 2 + 1];
        
        // Clear IF and TF
        regs->eflags &= ~(0x200 | 0x100);
        return;
    }
    
    /* TASKS.md L574: CLI / STI emulation */
    if (opcode == 0xFA) { // CLI - Clear Interrupt Flag
        /* Modify VIF (Virtual Interrupt Flag) instead of real IF */
        /* VIF is bit 19 (0x80000) in EFLAGS for VM86 */
        regs->eflags &= ~0x200;  /* Clear virtual IF */
        regs->eip += 1;          /* Advance past CLI opcode */
        return;
    }
    
    if (opcode == 0xFB) { // STI - Set Interrupt Flag  
        /* Set VIF (Virtual Interrupt Flag) */
        regs->eflags |= 0x200;   /* Set virtual IF */
        regs->eip += 1;          /* Advance past STI opcode */
        return;
    }
    
    /* TASKS.md L575: PUSHF / POPF emulation */
    if (opcode == 0x9C) { // PUSHF - Push FLAGS register
        uint16_t ss = regs->ss;
        uint16_t sp = regs->useresp;
        uint32_t stack_linear = (ss << 4) + sp;
        uint16_t *stack = (uint16_t *)vm86_linear_ptr(stack_linear - 2U, 2);
        if (!stack) {
            vm86_monitor_signal_fault(regs->eip, opcode);
            return;
        }

        stack[0] = (uint16_t)regs->eflags;
        
        regs->useresp = sp - 2;
        regs->eip += 1;
        return;
    }
    
    if (opcode == 0x9D) { // POPF - Pop FLAGS register
        uint16_t ss = regs->ss;
        uint16_t sp = regs->useresp;
        uint32_t stack_linear = (ss << 4) + sp;
        uint16_t *stack = (uint16_t *)vm86_linear_ptr(stack_linear, 2);
        if (!stack) {
            vm86_monitor_signal_fault(regs->eip, opcode);
            return;
        }
        
        /* Preserve VM, IOPL, and other privileged bits */
        uint32_t flags = *stack;
        regs->eflags = (regs->eflags & 0xFFFE3000) | (flags & ~0xFFFE3000);
        
        regs->useresp = sp + 2;
        regs->eip += 1;
        return;
    }

    if (opcode == 0xCF) { // IRET
        uint16_t ss = regs->ss;
        uint16_t sp = regs->useresp;
        uint16_t *stack = (uint16_t *)vm86_linear_ptr((ss << 4) + sp, 6);
        if (!stack) {
            vm86_monitor_signal_fault(regs->eip, opcode);
            return;
        }

        regs->eip = stack[0];
        regs->cs = stack[1];
        regs->eflags = (regs->eflags & 0xFFFF0000U) | stack[2];
        regs->useresp = sp + 6;
        return;
    }

    if (opcode == 0xE4) { // IN AL, imm8
        uint8_t port = code[1];
        regs->eax = (regs->eax & 0xFFFFFF00U) | vm86_port_space[port];
        regs->eip += 2;
        return;
    }

    if (opcode == 0xE6) { // OUT imm8, AL
        uint8_t port = code[1];
        vm86_port_space[port] = (uint8_t)(regs->eax & 0xFFU);
        regs->eip += 2;
        return;
    }

    if (opcode == 0xEC) { // IN AL, DX
        uint16_t port = (uint16_t)(regs->edx & 0xFFFFU);
        regs->eax = (regs->eax & 0xFFFFFF00U) | vm86_port_space[port];
        regs->eip += 1;
        return;
    }

    if (opcode == 0xEE) { // OUT DX, AL
        uint16_t port = (uint16_t)(regs->edx & 0xFFFFU);
        vm86_port_space[port] = (uint8_t)(regs->eax & 0xFFU);
        regs->eip += 1;
        return;
    }
    
    /* Handle HLT (0xF4) as exit from VM86 BIOS callback */
    if (opcode == 0xF4) {
        /* Check if we are in kernel-mode BIOS call */
        if (current_vm86_monitor && current_vm86_monitor->in_kernel_bios) {
             /* We need to return to 32-bit Protected Mode.
              * The stack currently has the VM86 IRET frame:
              * [GS] [FS] [DS] [ES] [SS] [ESP] [EFLAGS] [CS] [EIP]
              * We want to convert this to a Protected Mode IRET frame for return?
              * Actually, we want to return to the caller of sys_vm86 (start of function).
              * But we can't easily unwind C stack.
              * Best way: Modify the return IRET frame to point to a "landing pad" in kernel.
              * And ensure the stack layout matches what IRET expects for Ring 0 -> Ring 0 (or Ring 0 -> Ring 0 task switch?)
              * Wait, we are in Ring 0. We IRET'd to VM86 (simulated Ring 3 view).
              * Interrupt came in (Ring 3 -> Ring 0). Stack switched to TSS ESP0?
              * Yes. So we are on the kernel stack.
              * The stack contains the VM86 context.
              * If we strip the VM86 segments, clear VM bit in EFLAGS, 
              * and set CS:EIP to our C return point, we can resume C execution.
              */
             
             /* Struct of stack at regs ptr: */
             /* gs, fs, es, ds, ss, useresp... are in registers_t, but regs points to them on stack? */
             /* Note: registers_t in isr.S pushes GS, FS, ES, DS. */
             /* Then pushes EDI, ESI, EBP, ESP, EBX, EDX, ECX, EAX. */
             /* Then INT_NO, ERR_CODE. */
             /* Then EIP, CS, EFLAGS, USERESP, SS (if priv change) */
             /* For VM86, there IS a priv change (VM86 -> Ring 0). So SS, ESP are there. */
             /* And ES, DS, FS, GS are also pushed by CPU? No, only SS, ESP, EFLAGS, CS, EIP. */
             /* Wait, VM86 Exception Frame: */
             /* CPU pushes: GS, FS, DS, ES, SS, ESP, EFLAGS, CS, EIP. (All 32-bit extended). */
             /* So regs->eip is accessible. regs->cs is accessible. */
             /* Our registers_t definition assumes standard stack. */
             /* We need to be careful. */
             
             /* Simple approach: Use setjmp/longjmp style or a global flag? */
             /* We can't longjmp out of an interrupt handler easily without fixing stack. */
             /* But we are head of stack (mostly). */
             
             current_vm86_monitor->in_vm86 = 0;
             current_vm86_monitor->req_exit = 1;

             if (current_vm86_monitor->out_regs) {
                 struct vm86_regs *out = current_vm86_monitor->out_regs;
                 out->eax = regs->eax;
                 out->ebx = regs->ebx;
                 out->ecx = regs->ecx;
                 out->edx = regs->edx;
                 out->esi = regs->esi;
                 out->edi = regs->edi;
                 out->ebp = regs->ebp;
                 out->esp = regs->useresp;
                 out->eip = regs->eip;
                 out->cs  = (unsigned short)regs->cs;
                 out->eflags = regs->eflags;
                 out->ss = (unsigned short)regs->ss;

                 /*
                  * VM86 segments (ES, DS, FS, GS) are pushed by CPU after SS/ESP
                  * on the exception stack. They are located immediately above SS.
                  * Layout (High to Low): GS, FS, DS, ES, SS, ESP...
                  */
                 uint32_t *ext_stack = (uint32_t *)&regs->ss;
                 out->es = (unsigned short)ext_stack[1];
                 out->ds = (unsigned short)ext_stack[2];
                 out->fs = (unsigned short)ext_stack[3];
                 out->gs = (unsigned short)ext_stack[4];
             }
             
             /* We advance EIP to skip HLT, but we want to STOP. */
             /* Let's just return and let the monitor loop catch it? */
             /* If we are running a loop in C that called sys_vm86... wait. */
             /* sys_vm86 DOES NOT RETURN until manually constructed exit. */
             /* So we MUST change the return frame to "kernel mode". */
             
             /* 1. Clear VM bit (bit 17) in EFLAGS */
             regs->eflags &= ~0x20000;
             
             /*
              * Redirect return to the trampoline that cleans up the stack.
              * This trampoline (vm86_bios_ret_point) is defined in vm86_asm.S.
              * It expects to find the VM86 exception frame leftovers (GS, FS, DS, ES, SS, ESP)
              * on the stack after IRET pops EIP, CS, EFLAGS.
              */
             extern void vm86_bios_ret_point(void);
             regs->eip = (uint32_t)vm86_bios_ret_point;
             regs->cs = 0x08; // Kernel Code
             
             return;
        }
        
        kprint("VM86: HLT opcode in user VM86\n");
        vm86_monitor_signal_fault(regs->eip, opcode);
        return;
    }
    
    kprintf("VM86: Unhandled opcode in GPF (CS:IP %04X:%04X Op %02X)\n", 
           (unsigned)regs->cs, (unsigned)regs->eip, (unsigned)opcode);
    vm86_monitor_signal_fault(regs->eip, opcode);
}

/* Kernel BIOS Call Helper */
int vm86_bios_call(int int_no, struct vm86_regs *regs) {
    /* 1. Identity Map first 1MB (if not already) - Assumed done/safe? */
    /* Substrate Higher Half unmaps 0-4MB. We MUST remap it. */
    /* We can use 4MB pages or 4KB. */
    /* Let's assume we can temporarirly map it. */
    /* Map 0->0, 4K->4K ... up to 1MB. */
    pmap_t kpmap = pmap_kernel();
    for (uint32_t i = 0; i < 0x100000; i += 0x1000) {
        pmap_enter(kpmap, i, i, VM_PROT_ALL, 0); // User accessible for VM86
    }
    
    /* 2. Setup Monitor State */
    struct vm86_monitor mon;
    vm86_monitor_init(&mon);
    mon.in_kernel_bios = 1;
    mon.out_regs = regs;
    
    /* 3. Setup VM86 Struct */
    struct vm86_struct info;
    memset(&info, 0, sizeof(info));
    if (regs) info.regs = *regs;
    vm86_prepare_entry(&info);
    
    /* Setup Stack at 0x7C00 (safest area usually) or 0x1000 */
    /* We need to put our stub code somewhere too. 0x8000? */
    uint32_t stack_base = 0x2000;
    uint32_t code_base  = 0x1000;
    
    info.regs.esp = stack_base;
    info.regs.ss  = 0x0000;
    info.regs.cs  = 0x0000;
    info.regs.eip = code_base;
    
    /* 4. Write Stub Code: INT n; HLT */
    uint8_t *stub = vm86_linear_ptr(code_base, 3); // Identity mapped
    if (!stub) {
        return -EFAULT;
    }
    stub[0] = 0xCD;       // INT
    stub[1] = int_no;     // imm8
    stub[2] = 0xF4;       // HLT
    
    /* 5. Enter VM86 */
    /* This will verify segments and jump. It returns when HLT is hit via trampoline. */
    sys_vm86(&info);
    
    /* 6. Copy back registers */
    /*
     * The registers are copied back to `regs` in vm86_gpf_handler
     * when the HLT opcode is encountered.
     */
    
    return 0;
}
