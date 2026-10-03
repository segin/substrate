/*
 * machine/vmparam.h - kernel address-space constants of the architecture
 * being built: KERN_BASE (the direct map), KERNEL_VA_START, USER32_VA_END,
 * and the P2V/V2P translations
 *
 * Machine-independent code includes <machine/X.h> rather than naming an
 * architecture; the 64-bit kernel's Makefile defines SUBSTRATE_ARCH_X86_64.
 */
#ifdef SUBSTRATE_ARCH_X86_64
#include <arch/x86_64/vmparam.h>
#else
#include <arch/i386/vmparam.h>
#endif
