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

#endif
