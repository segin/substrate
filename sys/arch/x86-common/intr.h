#ifndef _ARCH_I386_INTR_H
#define _ARCH_I386_INTR_H

#include <stdint.h>

/*
 * EFLAGS save/restore.
 *
 * The kernel these serve is i386, but the headers are also compiled 64-bit:
 * tests/ builds the kernel sources natively for its host unit tests.  In long
 * mode a 32-bit push/pop is not merely mis-suffixed, it is unencodable --
 * PUSH/POP take a 16- or 64-bit operand only -- so `pushfl; popl %0` with a
 * uint32_t operand cannot be spelled at all there, and being in a header it
 * took the whole testsuite down with it.  The 64-bit path therefore goes
 * through a long temporary and narrows afterwards, which loses nothing:
 * EFLAGS is by definition the low 32 bits of RFLAGS.  The i386 path is
 * unchanged apart from dropping the now-redundant suffixes.
 */
#ifdef HOST_TEST
/*
 * Host unit tests build these kernel sources as an ordinary user program,
 * where cli/sti are privileged and fault instantly -- the process-exit test
 * died on the cli inside sq_lock() the moment it was linkable enough to run.
 * There are no interrupts to disable in a user process, so the whole family
 * is a no-op there and intr_enabled() reports the truth: nothing is masked.
 */
static inline uint32_t intr_disable(void) { return 0; }
static inline void intr_restore(uint32_t eflags) { (void)eflags; }
static inline void intr_enable(void) { }
static inline int intr_enabled(void) { return 1; }
#else
static inline uint32_t intr_disable(void) {
#if defined(__x86_64__)
    unsigned long rflags;
    __asm__ volatile ("pushf; pop %0; cli" : "=r" (rflags) : : "memory");
    return (uint32_t)rflags;
#else
    uint32_t eflags;
    __asm__ volatile ("pushf; pop %0; cli" : "=r" (eflags) : : "memory");
    return eflags;
#endif
}

static inline void intr_restore(uint32_t eflags) {
#if defined(__x86_64__)
    unsigned long rflags = eflags;
    __asm__ volatile ("push %0; popf" : : "r" (rflags) : "memory", "cc");
#else
    __asm__ volatile ("push %0; popf" : : "r" (eflags) : "memory", "cc");
#endif
}

static inline void intr_enable(void) {
    __asm__ volatile ("sti");
}

/*
 * True when local interrupts are currently enabled (EFLAGS.IF set).
 * A caller that finds IF clear is running either inside a hard
 * interrupt handler or an intr_disable()/spinlock_acquire_irq()
 * critical section — in both cases it MUST NOT sleep or yield.
 */
static inline int intr_enabled(void) {
#if defined(__x86_64__)
    unsigned long rflags;
    __asm__ volatile ("pushf; pop %0" : "=r" (rflags) : : "memory");
    return (rflags & 0x200ul) != 0;  /* bit 9 = IF */
#else
    uint32_t eflags;
    __asm__ volatile ("pushf; pop %0" : "=r" (eflags) : : "memory");
    return (eflags & 0x200u) != 0;   /* bit 9 = IF */
#endif
}
#endif /* HOST_TEST */

static inline void wait_for_interrupt(void) {
    /*
     * "Wait for an interrupt" is meaningless with interrupts masked: a hlt
     * executed with IF=0 never wakes — the CPU is frozen forever with no
     * console or serial output.  Enable interrupts before halting.
     *
     * This is not theoretical: the swapper idle loop (swapper_idle_loop)
     * reaches here having been context-switched in by another thread's
     * sched_yield(), which runs switch_to() under intr_disable().  switch_to
     * does not save/restore EFLAGS, so the swapper resumes with IF=0 and its
     * subsequent intr_restore() restores that captured IF=0 — a bare hlt here
     * then deadlocks the whole machine ("freezes on its own after a while,
     * even at the idle login prompt").
     *
     * sti enables interrupts only after the *following* instruction, so the
     * hlt is executed before any already-pending IRQ is taken — no wake-up is
     * lost in the sti/hlt window.  Mirrors sched_yield()'s own idle path.
     */
    __asm__ volatile ("sti; hlt");
}

#ifdef HOST_TEST
/*
 * Host stand-in for wait_for_interrupt().  `sti; hlt` is privileged, so the
 * host unit tests cannot run the real thing; each supplies its own definition
 * instead (tests/mocks.c, plus several host_test_*.c which longjmp out of the
 * idle loop from it).  Every one of them defined it and none declared it, so
 * the sole caller -- proc_idle_wait() in sys/pm/process.c -- had no prototype,
 * which C23 rejects.  Declared beside the function it substitutes for.
 */
void host_wait_for_interrupt(void);
#endif

struct thread;
void switch_to(struct thread *prev, struct thread *next);
void i386_load_gs_for_thread(struct thread *t);
void fork_child_return(void);
void new_kernel_thread_trampoline(void);
void new_user_thread_trampoline(void);
struct process;
int sched_clone_thread(struct process *proc, void *parent_regs, uint32_t tls_base, int *clear_child_tid);

#endif
