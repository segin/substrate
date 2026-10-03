/*
 * machine/cpu.h - CPU feature queries
 *
 * Machine-independent code includes <machine/X.h> rather than naming an
 * architecture.  CPUID and the TSC are the same on both x86 kernels, so
 * this one is shared.
 */
#include <arch/x86-common/cpu.h>
