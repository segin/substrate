#ifndef _SYS_VM86_H
#define _SYS_VM86_H

#include <stdint.h>

// VM86 return codes (Linux compatible)
#define VM86_SIGNAL     0   /* return due to signal */
#define VM86_UNKNOWN    1   /* unhandled GP fault - IO-instruction or similar */
#define VM86_INTx       2   /* int3/int x instruction (BH is the interrupt number) */
#define VM86_STI        3   /* sti/popf/iret instruction enabled virtual interrupts */

// VM86 sub-function codes (for vm86plus, simplified here)
#define VM86_ENTER      0
#define VM86_ENTER_NO_BYPASS 1
#define VM86_REQUEST_IRQ 2
#define VM86_FREE_IRQ   3
#define VM86_GET_IRQ_BITS 4
#define VM86_GET_AND_RESET_IRQ 5

// CPU type definition
#define CPU_286         2
#define CPU_386         3
#define CPU_486         4
#define CPU_586         5

struct vm86_regs {
    long ebx;
    long ecx;
    long edx;
    long esi;
    long edi;
    long ebp;
    long eax;
    long __null_ds;
    long __null_es;
    long __null_fs;
    long __null_gs;
    long orig_eax;
    long eip;
    unsigned short cs, __csh;
    long eflags;
    long esp;
    unsigned short ss, __ssh;
    unsigned short es, __esh;
    unsigned short ds, __dsh;
    unsigned short fs, __fsh;
    unsigned short gs, __gsh;
};

struct vm86_struct {
    struct vm86_regs regs;
    unsigned long screen_bitmap;   // Offset in process memory for screen bitmap (unused in basic)
};

// Trapframe structure matching kernel's stack layout for easy conversion
// This might need to align with your kernel's trapframe definition in sys/arch/i386/process.h or similar
// For now, vm86_regs covers the userspace view.

int vm86_bios_call(int int_no, struct vm86_regs *regs);

#endif
