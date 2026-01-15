#include <sys/types.h>
#include <sys/vm86.h>
#include <sys/sysarch.h>
#include <string.h>
#include <errno.h>
#include "../../kern/console.h"
#include "../../include/sys/proc.h"
#include "idt.h" 

// Access to current process/thread
extern struct process *current_process;
extern struct thread *current_thread;

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
    
    kprint("VM86: Unhandled opcode in GPF\n");
    /* TODO: Send signal to monitor process */
}
