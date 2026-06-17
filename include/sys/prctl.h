#ifndef _SYS_PRCTL_H
#define _SYS_PRCTL_H

/*
 * <sys/prctl.h> — process-control operations.
 *
 * substrate's native kernel has no prctl(2) syscall (only the Linux
 * personality emulates it).  This header provides the standard option
 * constants and the prototype so portable code compiles; the libc
 * prctl() returns -1/ENOSYS for options the native kernel cannot honour
 * (callers such as TDE's tdesud use PR_SET_DUMPABLE as a best-effort
 * hardening step and ignore the result).
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Option values mirror Linux's <linux/prctl.h> for source compatibility. */
#define PR_SET_PDEATHSIG   1
#define PR_GET_PDEATHSIG   2
#define PR_GET_DUMPABLE    3
#define PR_SET_DUMPABLE    4
#define PR_GET_KEEPCAPS    7
#define PR_SET_KEEPCAPS    8
#define PR_SET_NAME       15
#define PR_GET_NAME       16

int prctl(int option, ...);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_PRCTL_H */
