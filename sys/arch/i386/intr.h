#ifndef _ARCH_I386_INTR_H
#define _ARCH_I386_INTR_H

#include <stdint.h>

static inline uint32_t intr_disable(void) {
    uint32_t eflags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r" (eflags) : : "memory");
    return eflags;
}

static inline void intr_restore(uint32_t eflags) {
    __asm__ volatile ("pushl %0; popfl" : : "r" (eflags) : "memory", "cc");
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
    uint32_t eflags;
    __asm__ volatile ("pushfl; popl %0" : "=r" (eflags) : : "memory");
    return (eflags & 0x200u) != 0;   /* bit 9 = IF */
}

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

struct thread;
void switch_to(struct thread *prev, struct thread *next);
void i386_load_gs_for_thread(struct thread *t);
void fork_child_return(void);
void new_kernel_thread_trampoline(void);
void new_user_thread_trampoline(void);
struct process;
int sched_clone_thread(struct process *proc, void *parent_regs, uint32_t tls_base, int *clear_child_tid);

#endif
