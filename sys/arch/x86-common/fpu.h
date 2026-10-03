/*
 * fpu.h - lazy FPU context switching, shared by the i386 and x86_64 kernels
 */
#ifndef _ARCH_X86_COMMON_FPU_H
#define _ARCH_X86_COMMON_FPU_H

#include <stdint.h>
#include <machine/idt.h>

// FPU Status Word
#define SW_INVALID      0x0001
#define SW_DENORMAL     0x0002
#define SW_ZERODIVIDE   0x0004
#define SW_OVERFLOW     0x0008
#define SW_UNDERFLOW    0x0010
#define SW_PRECISION    0x0020
#define SW_STACK_FAULT  0x0040
#define SW_COND_CODE    0x0080
#define SW_C0           0x0100
#define SW_C1           0x0200
#define SW_C2           0x0400
#define SW_TOP          0x3800
#define SW_C3           0x4000
#define SW_BUSY         0x8000

// FPU Control Word
#define CW_INVALID      0x0001
#define CW_DENORMAL     0x0002
#define CW_ZERODIVIDE   0x0004
#define CW_OVERFLOW     0x0008
#define CW_UNDERFLOW    0x0010
#define CW_PRECISION    0x0020
// ... precision control, rounding control ...

// Forward declaration
struct process;

void fpu_init(void);
void fpu_handler(registers_t *regs);
void fpu_save_context(struct process *p);
void fpu_restore_context(struct process *p);
void fpu_switch(void);
void fpu_forget_process(struct process *p);
/* The current process's FXSAVE image (512 bytes), out to and back from a
 * signal frame.  fpu_signal_save() returns 0 if there is no FP state. */
int fpu_signal_save(void *image);
void fpu_signal_restore(const void *image);

#endif
