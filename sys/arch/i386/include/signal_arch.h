/*
 * sys/arch/i386/include/signal_arch.h - i386 Signal Context Structures
 */

#ifndef _ARCH_I386_SIGNAL_ARCH_H
#define _ARCH_I386_SIGNAL_ARCH_H

#include <stdint.h>

/*
 * Signal Context (sigcontext)
 * 
 * This structure is pushed onto the user stack during signal delivery.
 * It contains the saved state of the interrupted user thread.
 * When the signal handler returns, sys_sigreturn uses this to restore state.
 */
struct sigcontext {
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;       // Spurious, ignored by popad
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t trapno;
    uint32_t err;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t user_esp;
    uint32_t user_ss;
};

/*
 * Signal Frame (sigframe)
 * 
 * The actual stack layout seen by the signal handler.
 * 
 * Stack grows down:
 * [ ... higher addresses ... ]
 * [ struct sigcontext sc   ]  <- Saved context
 * [ int sig                ]  <- Argument 1: Signal number
 * [ void *return_addr      ]  <- Return address (trampoline)
 * [ ... lower addresses ... ]
 * 
 * Note: Check ABI for argument passing (stack vs regs). i386 System V passes args on stack.
 */
struct sigframe {
    uint32_t retaddr;       // Return address (trampoline)
    int      sig;           // Signal number (Argument 1)
    // struct sigcontext sc; // Context is usually passed by pointer or sits above args
    // To match typical BSD/Linux:
    // Handler(sig, code, scp) -> sigcontext is on stack, pointer passed as 3rd arg
    // But for simple handlers: void handler(int sig)
    
    // We will place sigcontext strictly AFTER the arguments for sigreturn to find it easily?
    // Actually, usually sigreturn takes a pointer, or we interpret stack pointer.
    
    // Simpler layout:
    // [ sigcontext ]
    // [ args       ]
    // [ retaddr    ]
    
    // Let's use:
    // [ sigcontext  ] (at esp + offset)
    // [ sig         ] (at esp + 4)
    // [ retaddr     ] (at esp) -> points to trampoline
    
    struct sigcontext sc;
};

#endif /* _ARCH_I386_SIGNAL_ARCH_H */
