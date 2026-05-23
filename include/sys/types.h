#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

/* Declare ssize_t and off_t up front using compiler builtins.
 * Rationale: when this header is reached via a re-entrant path
 * (gnulib's stdint.h shim pulls in wchar.h which pulls in stdio.h
 * which pulls in sys/types.h), our own _SYS_TYPES_H guard fires and
 * any typedef placed below the includes is skipped — leaving stdio.h
 * staring at undeclared ssize_t/off_t when it tries to prototype
 * getline / fseeko / ftello.  Putting the typedefs above all our
 * own includes makes them visible to that re-entrant stdio.h pass.
 * The later int32_t/int64_t-based typedefs reduce to the same type
 * (C11 6.7.3 allows redeclaration of a typedef to the same type).  */
typedef __SIZE_TYPE__   __sz_internal_size_t__;
#ifndef _SSIZE_T_DECLARED
#define _SSIZE_T_DECLARED
typedef __INT32_TYPE__  ssize_t;
#endif
#ifndef _OFF_T_DECLARED
#define _OFF_T_DECLARED
typedef __INT64_TYPE__  off_t;
#endif

#include <stdint.h>
#include <stddef.h>

#if defined(__x86_64__) || defined(_M_X64)
#define SUBSTRATE_PID_MAX 9999999
#define SUBSTRATE_TID_MAX 9999999
#else
#define SUBSTRATE_PID_MAX 99999
#define SUBSTRATE_TID_MAX 99999
#endif

typedef int32_t pid_t;
typedef int32_t tid_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
/* glibc-style internal aliases that ported code reaches for. */
typedef uid_t __uid_t;
typedef gid_t __gid_t;
typedef pid_t __pid_t;
/* off_t / ssize_t already declared at top of header via compiler
 * builtins for re-entrant safety; skip the duplicate typedefs.  */
typedef int64_t blkcnt_t;
typedef uint64_t ino_t;
typedef uint32_t nlink_t;
typedef uint32_t blksize_t;
// size_t from stddef.h
typedef int32_t mode_t;
typedef uint32_t dev_t;
typedef int64_t time_t;
typedef long fpos_t;

// Additional POSIX types
typedef uint32_t clock_t;
typedef int32_t  clockid_t;
typedef int32_t  timer_t;
typedef int64_t  useconds_t;
typedef int64_t  suseconds_t;
typedef uint32_t id_t;
typedef int32_t  key_t;

typedef uint64_t fsblkcnt_t;
typedef uint64_t fsfilcnt_t;

/* Pthread types live in <pthread.h>.  They're declared there so the
 * struct shape of pthread_cond_t (a futex-backed seq counter, not
 * an int) is visible to every translation unit that #includes
 * pthread.h.  Keeping the typedefs duplicated here as `int32_t`
 * conflicts with the real struct shape and silently breaks any code
 * that pulls in sys/types.h *and* pthread.h. */
typedef int32_t  pthread_rwlock_t;
typedef int32_t  pthread_rwlockattr_t;
typedef int32_t  pthread_spinlock_t;
typedef int32_t  pthread_barrier_t;
typedef int32_t  pthread_barrierattr_t;

// BSD/Legacy
typedef uint32_t vm_offset_t;
typedef uint32_t vm_size_t;

/* BSD short-form unsigned integer aliases — historically in
 * <sys/types.h> on every BSD-derived system.  Many ported daemons
 * (inetutils' libinetutils, tftp, talk, sendmail-derived stuff)
 * use these without bothering to typedef them locally.  */
typedef unsigned char       u_char;
typedef unsigned short      u_short;
typedef unsigned int        u_int;
typedef unsigned long       u_long;
typedef unsigned long long  u_quad_t;
typedef long long           quad_t;
typedef uint8_t             u_int8_t;
typedef uint16_t            u_int16_t;
typedef uint32_t            u_int32_t;
typedef uint64_t            u_int64_t;
typedef char *              caddr_t;        /* core address */
typedef long                daddr_t;        /* disk address */

#ifdef __cplusplus
}
#endif
#endif
