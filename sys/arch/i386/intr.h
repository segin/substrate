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
    __asm__ volatile ("hlt");
}

#endif
