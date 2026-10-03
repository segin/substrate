/*
 * machine/syscall.h - system-call numbers and conventions
 *
 * Machine-independent code includes <machine/X.h> rather than naming an
 * architecture.  The x86_64 kernel serves 32-bit processes, which call it
 * through int $0x80 with the i386 ABI unchanged (docs/specs/abi-amd64.md,
 * section 10), so both kernels use the i386 definitions.
 */
#include <arch/i386/syscall.h>
