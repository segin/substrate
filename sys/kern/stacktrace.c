/*
 * stacktrace.c - Stack frame unwinding for kernel debugging
 *
 * Implements EBP-chain stack trace unwinding as required by TASKS.md L563.
 * Displays call stack on panic or debug request.
 */

#include <stdint.h>
#include <stdio.h>
#include "console.h"

/* Maximum number of frames to trace */
#define MAX_STACK_FRAMES 16

/* Stack frame structure (standard x86 calling convention) */
struct stack_frame {
    struct stack_frame *ebp;  /* Saved EBP (previous frame) */
    uint32_t eip;             /* Return address */
};

/*
 * stack_trace - Print stack trace from current EBP
 *
 * Walks the EBP chain and prints each frame's return address.
 * Stops at NULL EBP or after MAX_STACK_FRAMES frames.
 */
void stack_trace(void) {
    struct stack_frame *frame;
    char buf[80];
    int depth = 0;
    
    /* Get current EBP */
    __asm__ volatile("mov %%ebp, %0" : "=r"(frame));
    
    kprint("\n--- Stack Trace ---\n");
    
    while (frame && depth < MAX_STACK_FRAMES) {
        /* Validate frame pointer is in kernel space (0xC0000000+) to prevent crashes */
        if ((uint32_t)frame < 0xC0000000 || (uint32_t)frame > 0xFFFFFFFF - sizeof(*frame)) {
            sprintf(buf, "  #%d: [Invalid frame pointer 0x%08x]\n", depth, (uint32_t)frame);
            kprint(buf);
            break;
        }
        
        /* Check alignment (EBP should be 4-byte aligned) */
        if ((uint32_t)frame & 0x3) {
            sprintf(buf, "  #%d: [Misaligned frame 0x%08x]\n", depth, (uint32_t)frame);
            kprint(buf);
            break;
        }
        
        sprintf(buf, "  #%d: EIP=0x%08x  EBP=0x%08x\n", 
                depth, frame->eip, (uint32_t)frame->ebp);
        kprint(buf);
        
        /* Move to previous frame */
        frame = frame->ebp;
        depth++;
    }
    
    if (depth == 0) {
        kprint("  (no frames available)\n");
    } else if (depth >= MAX_STACK_FRAMES) {
        kprint("  ... (truncated)\n");
    }
    
    kprint("--- End Stack Trace ---\n");
}

/*
 * stack_trace_from - Print stack trace starting from given EBP/EIP
 *
 * Used for exception handlers where we have saved register context.
 */
void stack_trace_from(uint32_t ebp, uint32_t eip) {
    struct stack_frame *frame = (struct stack_frame *)ebp;
    char buf[80];
    int depth = 0;
    
    kprint("\n--- Stack Trace ---\n");
    
    /* Print the faulting address first */
    sprintf(buf, "  #%d: EIP=0x%08x (faulting address)\n", depth, eip);
    kprint(buf);
    depth++;
    
    while (frame && depth < MAX_STACK_FRAMES) {
        /* Validate frame pointer */
        if ((uint32_t)frame < 0xC0000000 || (uint32_t)frame > 0xFFFFFFFF - sizeof(*frame)) {
            sprintf(buf, "  #%d: [Invalid frame 0x%08x]\n", depth, (uint32_t)frame);
            kprint(buf);
            break;
        }
        
        if ((uint32_t)frame & 0x3) {
            sprintf(buf, "  #%d: [Misaligned frame 0x%08x]\n", depth, (uint32_t)frame);
            kprint(buf);
            break;
        }
        
        sprintf(buf, "  #%d: EIP=0x%08x  EBP=0x%08x\n", 
                depth, frame->eip, (uint32_t)frame->ebp);
        kprint(buf);
        
        frame = frame->ebp;
        depth++;
    }
    
    if (depth >= MAX_STACK_FRAMES) {
        kprint("  ... (truncated)\n");
    }
    
    kprint("--- End Stack Trace ---\n");
}
