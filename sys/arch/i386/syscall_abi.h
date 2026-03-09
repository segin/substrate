#ifndef _ARCH_I386_SYSCALL_ABI_H
#define _ARCH_I386_SYSCALL_ABI_H

#include <stdint.h>
#include <string.h>
#include <arch/i386/idt.h>
#include <exec/perso/personality.h>

static inline void i386_extract_syscall_args(const struct personality *p,
                                             const registers_t *regs,
                                             uint32_t *args) {
    if (!args) {
        return;
    }

    memset(args, 0, sizeof(uint32_t) * 8);
    if (!regs) {
        return;
    }

    if (p && (p->id == PERS_FREEBSD || p->id == PERS_NATIVE || p->id == PERS_SVR4)) {
        const uint32_t *user_stack = (const uint32_t *)(uintptr_t)regs->useresp;

        args[0] = user_stack[1];
        args[1] = user_stack[2];
        args[2] = user_stack[3];
        args[3] = user_stack[4];
        args[4] = user_stack[5];
        args[5] = user_stack[6];
        args[6] = user_stack[7];
        args[7] = user_stack[8];
        return;
    }

    if (p && p->id == PERS_ELKS) {
        args[0] = regs->ebx;
        args[1] = regs->ecx;
        args[2] = regs->edx;
        args[3] = regs->edi;
        args[4] = regs->esi;
        return;
    }

    args[0] = regs->ebx;
    args[1] = regs->ecx;
    args[2] = regs->edx;
    args[3] = regs->esi;
    args[4] = regs->edi;
    args[5] = regs->ebp;
}

#endif
