#ifndef _ARCH_I386_SYSCALL_ABI_H
#define _ARCH_I386_SYSCALL_ABI_H

#include <stdint.h>
#include <string.h>
#include <arch/i386/idt.h>
#include <exec/perso/personality.h>
#include <sys/copy.h>

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

    if (p && (p->id == PERS_FREEBSD || p->id == PERS_NETBSD ||
              p->id == PERS_OPENBSD || p->id == PERS_NATIVE ||
              p->id == PERS_SVR4)) {
        /* NetBSD and OpenBSD use the same stack-based int $0x80 ABI as
         * FreeBSD: caller pushes args, libc stubs do `mov $nr, %eax;
         * int $0x80` with no register marshalling.  Without this branch
         * NetBSD binaries fell through to the register-based extraction
         * below and saw garbage args (manifested as ld.elf_so's mmap()
         * appearing to "succeed" with bogus low addresses → SIGSEGV in
         * imalloc on first writeback). */
        /*
         * Args are the cdecl-pushed words at useresp[1..8] (useresp[0] is
         * the libc stub's return address).  Read them fault-safely via
         * copyin: a thread whose esp sits within 0x20 of the TOP of its
         * (pthread) stack mapping would otherwise have this unconditional
         * 8-word read overrun the mapping and page-fault the KERNEL while
         * extracting args -- a userspace stack state must never panic the
         * kernel.  Real syscalls use <= 6 args, so the overrun words are
         * unused; on the rare near-top esp the bulk read fails and we fall
         * back to word-by-word, stopping at the first unreadable word (the
         * remaining args stay zero from the memset above).
         */
        const char *sp = (const char *)(uintptr_t)regs->useresp + sizeof(uint32_t);
        if (copyin(sp, args, 8 * sizeof(uint32_t)) != 0) {
            memset(args, 0, sizeof(uint32_t) * 8);
            for (int i = 0; i < 8; i++) {
                if (copyin(sp + (size_t)i * sizeof(uint32_t),
                           &args[i], sizeof(uint32_t)) != 0)
                    break;
            }
        }
        return;
    }

    if (p && p->id == PERS_ELKS) {
        args[0] = (uint16_t)regs->ebx;
        args[1] = (uint16_t)regs->ecx;
        args[2] = (uint16_t)regs->edx;
        args[3] = (uint16_t)regs->edi;
        args[4] = (uint16_t)regs->esi;
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
