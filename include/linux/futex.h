/*
 * <linux/futex.h> — Linux-API compat header for substrate.
 *
 * Ported software (libxshmfence, libdrm, mesa, ...) expects to find
 * the kernel-side futex op-codes (FUTEX_WAIT / FUTEX_WAKE / ...) and
 * the SYS_futex syscall number in this Linux-canonical location.
 * Substrate's own header is <sys/futex.h>; this is a thin alias that
 * also adds the syscall-number alias the upstream code uses.
 */
#ifndef _LINUX_FUTEX_H
#define _LINUX_FUTEX_H

#include <sys/futex.h>
#include <sys/syscall.h>

/* Linux op-codes the substrate kernel doesn't (yet) implement —
 * defined so ported code compiles, but using them returns -ENOSYS
 * from the substrate futex syscall.  FUTEX_WAIT / FUTEX_WAKE are
 * the ones libpthread + libxshmfence actually rely on. */
#ifndef FUTEX_PRIVATE_FLAG
#define FUTEX_PRIVATE_FLAG    128
#endif
#ifndef FUTEX_CLOCK_REALTIME
#define FUTEX_CLOCK_REALTIME  256
#endif
#ifndef FUTEX_CMD_MASK
#define FUTEX_CMD_MASK        ~(FUTEX_PRIVATE_FLAG | FUTEX_CLOCK_REALTIME)
#endif

/* substrate's syscall.h spells the futex number SYS_FUTEX; the Linux
 * libc convention (which ported code follows) is lowercase. */
#ifndef SYS_futex
#define SYS_futex SYS_FUTEX
#endif

#endif /* _LINUX_FUTEX_H */
