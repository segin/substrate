/*
 * signal_arch.h - signal frames on the x86_64 kernel
 *
 * Today's userland is 32-bit, and a 32-bit process gets exactly the i386
 * signal ABI -- sigcontext, ucontext and the trampoline addresses -- in
 * compatibility mode (docs/specs/abi-amd64.md, section 10).  The native
 * amd64 frames of section 7 arrive with the 64-bit userland.
 */
#ifndef _ARCH_X86_64_SIGNAL_ARCH_H
#define _ARCH_X86_64_SIGNAL_ARCH_H

#include <arch/i386/signal_arch.h>

#endif
