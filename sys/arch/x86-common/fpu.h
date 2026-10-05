/*
 * fpu.h - lazy FPU context switching, shared by the i386 and x86_64 kernels
 */
#ifndef _ARCH_X86_COMMON_FPU_H
#define _ARCH_X86_COMMON_FPU_H

#include <stddef.h>
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

// Forward declarations
struct process;
struct thread;

void fpu_init(void);
void fpu_handler(registers_t *regs);
/* NEXT is being scheduled on this CPU: let it use the FPU only if the live
 * registers are its own. */
void fpu_switch(struct thread *next);
void fpu_forget_process(struct process *p);
/* A new thread starts with a copy of its creator's state; a thread being
 * freed gives its save area back. */
void fpu_thread_inherit(struct thread *parent, struct thread *child);
void fpu_thread_free(struct thread *t);
/*
 * The current thread's state, out to and back from a signal frame: the
 * FXSAVE image (512 bytes) in the frame itself, and, with XSAVE, a block of
 * fpu_signal_extra_len() bytes in user memory for what follows it (the
 * XSAVE header and the extended components).  fpu_signal_save() returns 0
 * if there is no state; see fpu.c for the order these must be called in.
 */
int fpu_signal_save(void *image);
size_t fpu_signal_extra_len(void);
int fpu_signal_copyout_extra(void *uaddr);
int fpu_signal_restore(int format, const void *image, const void *uextra,
                       size_t extra_len);

/* What fpu_signal_save() put in the image.  The i386 mcontext's mc_fpformat
 * holds these values. */
#define FPU_SIG_NONE    0
#define FPU_SIG_FNSAVE  1       /* 108 bytes, x87 only */
#define FPU_SIG_FXSAVE  2       /* 512 bytes */

/* execve(2): back to the initial state. */
void fpu_thread_reset(struct thread *t);

/* ptrace(2): a stopped thread's registers in one of these layouts, to and
 * from a kernel buffer of exactly the layout's size.  -EIO if the CPU does
 * not save in that layout. */
#define FPU_REGS_FNSAVE 1       /* 108 bytes; only on a CPU without FXSAVE */
#define FPU_REGS_FXSAVE 2       /* 512 bytes */
#define FPU_REGS_XSAVE  3       /* fpu_xstate_info()'s length */
#define FPU_REGS_MAX    4096    /* no layout is larger */
int fpu_thread_get_regs(struct thread *t, int layout, void *kbuf, size_t len);
int fpu_thread_set_regs(struct thread *t, int layout, const void *kbuf,
                        size_t len);
/* The components XSAVE keeps (XCR0) and the size of its area. */
int fpu_xstate_info(uint64_t *xcr0, uint32_t *len);

#endif
