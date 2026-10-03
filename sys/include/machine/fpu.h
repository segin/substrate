/*
 * machine/fpu_emu.h - FPU state and x87 emulation of the architecture being
 * built
 *
 * Machine-independent code includes <machine/X.h> rather than naming an
 * architecture; the 64-bit kernel's Makefile defines SUBSTRATE_ARCH_X86_64.
 */
#ifdef SUBSTRATE_ARCH_X86_64
#include <arch/x86_64/fpu.h>
#else
#include <arch/i386/fpu/fpu_emu.h>
#endif
