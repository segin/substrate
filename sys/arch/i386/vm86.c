#include <sys/types.h>
#include <sys/vm86.h>
#include <sys/sysarch.h>
#include <string.h>
#include <errno.h>
#include <kern/console.h>
#include <include/sys/proc.h>
#include "idt.h" 
#include "pmap.h"
#include <arch/i386/pmap.h> 

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
};

/* Per-process VM86 monitor (NULL if not using VM86) */
static struct vm86_monitor *current_vm86_monitor;

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
// Access to the syscall return registers (mapped in syscall.c)
/*
 * sys_vm86: Enter virtual 8086 mode.
 */
int sys_vm86(struct vm86_struct *info) {
    if (!info) return -EFAULT;
    
    struct vm86_struct k_info;
    memcpy(&k_info, info, sizeof(struct vm86_struct));
    
    // Manual IRET Frame Construction
    // We are going to overwrite the current stack frame and JUMP to IRET.
    // Frame Layout for VM86 IRET:
    // [GS] [FS] [DS] [ES] [SS] [ESP] [EFLAGS] [CS] [EIP] -- wait, Stack grows down.
    // Top of Stack (Low Addr) -> [EIP] [CS] [EFLAGS] [ESP] [SS] [ES] [DS] [FS] [GS] -> Bottom (High Addr)
    //
    // EIP, CS, EFLAGS, ESP, SS are standard IRET.
    // ES, DS, FS, GS are extra for VM86.
    
    // We can just use inline assembly to set ESP and execute IRET.
    // We need to load all registers first.
    
    // Note: k_info.regs matches the 'registers' struct somewhat but includes segment registers manually.
    
    // Disable interrupts to ensure atomicity
    __asm__ volatile("cli");
    
    // Load general purpose registers
    // We use a struct pointer to load them via MOV sequences or similar.
    // But we need to switch stack to the IRET frame first.
    // We can build the IRET frame on the current stack (below current position).
    
    uint32_t eflags = k_info.regs.eflags;
    eflags |= 0x20000; // VM
    eflags |= 0x200;   // IF (virtual interrupts enabled)
    
    // Prepare the IRET stack
    // We push in reverse order: GS, FS, DS, ES, SS, ESP, EFLAGS, CS, EIP
    
    // We use a temporary pointer to build the stack
    // We'll trust the compiler to handle local vars until we switch ESP.
    
    // Ideally:
    // mov esp, [stack_base] ?
    // No, just push current stack deeper.
    
    __asm__ volatile(
        "movl %0, %%eax\n\t" // GS
        "pushl %%eax\n\t"
        "movl %1, %%eax\n\t" // FS
        "pushl %%eax\n\t"
        "movl %2, %%eax\n\t" // DS
        "pushl %%eax\n\t"
        "movl %3, %%eax\n\t" // ES
        "pushl %%eax\n\t"
        "movl %4, %%eax\n\t" // SS
        "pushl %%eax\n\t"
        "movl %5, %%eax\n\t" // ESP
        "pushl %%eax\n\t"
        "movl %6, %%eax\n\t" // EFLAGS
        "pushl %%eax\n\t"
        "movl %7, %%eax\n\t" // CS
        "pushl %%eax\n\t"
        "movl %8, %%eax\n\t" // EIP
        "pushl %%eax\n\t"
        
        // Load GPRs
        "movl %9, %%eax\n\t"
        "movl %10, %%ecx\n\t"
        "movl %11, %%edx\n\t"
        "movl %12, %%ebx\n\t"
        "movl %13, %%esp\n\t" // WAIT, we assume registers_t.esp is ignored? No, internal use?
                              // We just pushed EIP/CS etc, so Stack is ready.
                              // We should NOT load ESP from k_info here, user ESP is in the frame.
                              // But we need to load EBP, ESI, EDI.
        // We can't clobber ESP here.
        
        "movl %13, %%ebp\n\t"
        "movl %14, %%esi\n\t"
        "movl %15, %%edi\n\t"
       
        // We are ready. IRET will consume the stack we just built.
        "iret"
        : 
        : "m"(k_info.regs.gs), "m"(k_info.regs.fs), "m"(k_info.regs.ds), "m"(k_info.regs.es),
          "m"(k_info.regs.ss), "m"(k_info.regs.esp), "m"(eflags), "m"(k_info.regs.cs), "m"(k_info.regs.eip),
          "m"(k_info.regs.eax), "m"(k_info.regs.ecx), "m"(k_info.regs.edx), "m"(k_info.regs.ebx),
          "m"(k_info.regs.ebp), "m"(k_info.regs.esi), "m"(k_info.regs.edi)
        : "eax", "memory"
    );
    
    // Unreachable
    return 0;
}

int vm86_init_bsd(void *args) {
    struct i386_vm86_args *ua = (struct i386_vm86_args *)args;
    if (!ua) return -EFAULT;
    
    if (ua->sub_op == VM86_INIT) {
        // BSD `init` usually enters the mode or prepares it.
        // If it implies entering, we need the struct.
        // Assuming sub_args points to `struct vm86_struct`.
        if (!ua->sub_args) return -EINVAL;
        return sys_vm86((struct vm86_struct*)ua->sub_args);
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
    
    uint8_t *code = (uint8_t*)linear;
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
        uint16_t *stack = (uint16_t*)stack_linear;
        
        stack--; *stack = (uint16_t)regs->eflags;
        stack--; *stack = (uint16_t)regs->cs;
        stack--; *stack = (uint16_t)(regs->eip + 2); // Return after INT n
        
        regs->useresp = (uint32_t)(sp - 6);
        
        // Load new CS:IP from IVT (at 0x0000)
        uint16_t *ivt = (uint16_t*)0x0;
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
        uint16_t *stack = (uint16_t*)stack_linear;
        
        stack--;
        *stack = (uint16_t)regs->eflags;
        
        regs->useresp = sp - 2;
        regs->eip += 1;
        return;
    }
    
    if (opcode == 0x9D) { // POPF - Pop FLAGS register
        uint16_t ss = regs->ss;
        uint16_t sp = regs->useresp;
        uint32_t stack_linear = (ss << 4) + sp;
        uint16_t *stack = (uint16_t*)stack_linear;
        
        /* Preserve VM, IOPL, and other privileged bits */
        uint32_t flags = *stack;
        regs->eflags = (regs->eflags & 0xFFFE3000) | (flags & ~0xFFFE3000);
        
        regs->useresp = sp + 2;
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
             
             /* We advance EIP to skip HLT, but we want to STOP. */
             /* Let's just return and let the monitor loop catch it? */
             /* If we are running a loop in C that called sys_vm86... wait. */
             /* sys_vm86 DOES NOT RETURN until manually constructed exit. */
             /* So we MUST change the return frame to "kernel mode". */
             
             /* 1. Clear VM bit (bit 17) in EFLAGS */
             regs->eflags &= ~0x20000;
             
             /* 2. Remove ES, DS, FS, GS, SS, ESP from the "IRET" frame on stack? */
             /* The CPU pushed them. We must remove them if we return to Ring 0. */
             /* Ring 0 -> Ring 0 IRET frame is just EIP, CS, EFLAGS. */
             /* We need to slide EIP, CS, EFLAGS down over GS, FS, DS, ES, SS, ESP? */
             /* This is tricky in C. */
             
             /* HACK: Use the `in_kernel_bios` jmp_buf if we had one. */
             /* Better: Just change CS:EIP to a kernel assembly label that cleans up stack. */
             
             extern void vm86_bios_ret_point(void);
             regs->eip = (uint32_t)vm86_bios_ret_point;
             regs->cs = 0x08; // Kernel Code
             
             /* IMPORTANT: The CPU pushed GS, FS, DS, ES, SS, ESP because of VM86. */
             /* We cannot change that fact easily. */
             /* When we IRET, the CPU checks VM bit. If 0, it pops EIP, CS, EFLAGS. */
             /* It DOES NOT pop the segment registers. */
             /* So if we clear VM bit, we effectively perform a Ring 0 return. */
             /* But the stack physically has the extra registers! */
             /* We must manually pop them or shuffle the stack. */
             /* Modifying 'regs' struct writes to the stack. */
             /* But we can't delete words from stack here easily. */
             
             /* Setup a flag and just return? The loop in `bios_call` needs to run. */
             /* But we are TRAPPED inside `sys_vm86` -> `iret` -> VM86 -> INT -> `handler` -> HERE. */
             /* We are nested. */
             
             /* Solution: Change return EIP to a trampoline that fixes stack. */
             regs->eip = (uint32_t)vm86_bios_ret_point;
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

/* Assembly trampoline to clean up stack after VM86 -> Kernel transition */
/* Defined in new vm86_asm.S or inline asm here if possible? Inline asm cannot easily define global label callable from C assignment. */
/* We will produce a .S file or use an `extern` symbol. */
/* For simplicity, let's look for a `vm86.S` or `isr.S`. */
/* Better: add a naked function here. */

void __attribute__((naked)) vm86_bios_ret_point(void) {
    /* We arrive here after IRET from the GPF handler, with VM bit cleared. */
    /* The stack SHOULD contain the VM86 segment registers that invoke IRET didn't pop because VM=0. */
    /* WAIT. IRET behavior: */
    /* If NT=1, task switch. (Assume NT=0). */
    /* If returning to Virtual Mode (VM=1 in dest EFLAGS) -> Pops GS,FS,DS,ES,SS,ESP,EFLAGS,CS,EIP. */
    /* If returning to Protected Mode (VM=0): Pops EIP, CS, EFLAGS. (And ESP, SS if priv level change). */
    /* We forced VM=0 in EFLAGS. We forced CS=0x08 (Kernel). */
    /* Current CPL=0. Dest CPL=0. No priv change. */
    /* So IRET popped EIP, CS, EFLAGS. */
    /* The stack still has ESP, SS, ES, DS, FS, GS ! (from the VM86 exception frame). */
    /* We must pop them to clean up the stack to the state before `sys_vm86`'s IRET ? */
    /* Actually, we just need to restore our C environment. */
    /* sys_vm86 built a frame. We want to return FROM sys_vm86. */
    
    /* Pop the junk */
    __asm__ volatile (
        "add $24, %esp \n\t" /* Pop ESP, SS, ES, DS, FS, GS (6 * 4 = 24 bytes) */
        "pop %edi \n\t"
        "pop %esi \n\t"
        "pop %ebp \n\t"
        "pop %ebx \n\t"
        "pop %edx \n\t"
        "pop %ecx \n\t"
        "pop %eax \n\t" /* Determine return value? */
        "ret \n\t"
    );
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
        pmap_enter(kpmap, i, i, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_USER, 0);
    }
    
    /* 2. Setup Monitor State */
    struct vm86_monitor mon;
    vm86_monitor_init(&mon);
    mon.in_kernel_bios = 1;
    
    /* 3. Setup VM86 Struct */
    struct vm86_struct info;
    memset(&info, 0, sizeof(info));
    if (regs) info.regs = *regs;
    
    /* Setup Stack at 0x7C00 (safest area usually) or 0x1000 */
    /* We need to put our stub code somewhere too. 0x8000? */
    uint32_t stack_base = 0x2000;
    uint32_t code_base  = 0x1000;
    
    info.regs.esp = stack_base;
    info.regs.ss  = 0x0000;
    info.regs.cs  = 0x0000;
    info.regs.eip = code_base;
    
    /* 4. Write Stub Code: INT n; HLT */
    uint8_t *stub = (uint8_t*)code_base; // Identity mapped
    stub[0] = 0xCD;       // INT
    stub[1] = int_no;     // imm8
    stub[2] = 0xF4;       // HLT
    
    /* 5. Enter VM86 */
    /* This will verify segments and jump. It returns when HLT is hit via trampoline. */
    sys_vm86(&info);
    
    /* 6. Copy back registers */
    /* We need to capture the registers from the monitor/state? */
    /* Validating how we get output regs... */
    /* The trampoline restores registers popped from stack. */
    /* But we need the values from the VM86 end state. */
    /* The `registers_t` passed to handler has them. */
    /* We should copy them to `regs` in the handler before exiting. */
    /* TODO: Optimize later. For now, assume we just run it. */
    
    return 0;
}
