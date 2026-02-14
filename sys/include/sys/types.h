#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H

#ifdef HOST_TEST
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include_next <sys/types.h>
#include <stdint.h>
#include <stddef.h>

#ifndef _SYS_TYPES_H_SUBSTRATE_EXT
#define _SYS_TYPES_H_SUBSTRATE_EXT

// Substrate-specific types for host mocks
typedef uint32_t kdev_t;
typedef uint32_t vm_offset_t;
typedef uint32_t vm_size_t;
typedef int32_t  tid_t;

// Ensure standard POSIX types are available
// These should usually be in the host's <sys/types.h>
// but we define them here if the host headers are being difficult.
#ifndef _UID_T_DEFINED
#define _UID_T_DEFINED
typedef uint32_t uid_t;
#endif
#ifndef _GID_T_DEFINED
#define _GID_T_DEFINED
typedef uint32_t gid_t;
#endif
#ifndef _ID_T_DEFINED
#define _ID_T_DEFINED
typedef uint32_t id_t;
#endif
#ifndef _MODE_T_DEFINED
#define _MODE_T_DEFINED
typedef uint32_t mode_t;
#endif
// Skip defining off_t on Linux as it conflicts with glibc's definition
// Linux's off_t is fine for our purposes
#ifndef __linux__
#ifndef _OFF_T_DEFINED
#define _OFF_T_DEFINED
typedef long long off_t;
#endif
#endif
#ifndef _PID_T_DEFINED
#define _PID_T_DEFINED
typedef int pid_t;
#endif
#ifndef _REGISTER_T_DEFINED
#define _REGISTER_T_DEFINED
typedef int32_t register_t;
#endif

// Clock types might be missing if <time.h> was NOT included by the host <sys/types.h>
#ifndef _CLOCK_T_DEFINED
#define _CLOCK_T_DEFINED
typedef long clock_t;
#endif
#ifndef _TIME_T_DEFINED
#define _TIME_T_DEFINED
typedef long time_t;
#endif
#ifndef _CLOCKID_T_DEFINED
#define _CLOCKID_T_DEFINED
typedef int clockid_t;
#endif
#ifndef _TIMER_T_DEFINED
#define _TIMER_T_DEFINED
typedef void * timer_t;
#endif
#ifndef _INO_T_DEFINED
#define _INO_T_DEFINED
typedef uint64_t ino_t;
#endif
#ifndef _BLKCNT_T_DEFINED
#define _BLKCNT_T_DEFINED
typedef int64_t blkcnt_t;
#endif
#ifndef _NLINK_T_DEFINED
#define _NLINK_T_DEFINED
typedef uint32_t nlink_t;
#endif
#ifndef _BLKSIZE_T_DEFINED
#define _BLKSIZE_T_DEFINED
typedef uint32_t blksize_t;
#endif
#ifndef _DEV_T_DEFINED
#define _DEV_T_DEFINED
typedef uint32_t dev_t;
#endif
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef long ssize_t;
#endif

#endif // _SYS_TYPES_H_SUBSTRATE_EXT

#else // !HOST_TEST
// Substrate Native Types
#include <stdint.h>
#include <stddef.h>

typedef int32_t pid_t;
typedef int32_t tid_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef int32_t register_t;

typedef int64_t off_t;
typedef int64_t time_t;
typedef long fpos_t;

typedef int32_t mode_t;
typedef uint32_t dev_t;
typedef uint64_t ino_t;
typedef uint32_t nlink_t;
typedef uint32_t blksize_t;
typedef int64_t blkcnt_t;

typedef int32_t ssize_t;
typedef uint32_t kdev_t; // Kernel internal device type

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

// Pthread types
typedef int32_t  pthread_t;
typedef int32_t  pthread_attr_t;
typedef int32_t  pthread_mutex_t;
typedef int32_t  pthread_mutexattr_t;
typedef int32_t  pthread_cond_t;
typedef int32_t  pthread_condattr_t;
typedef int32_t  pthread_key_t;
typedef int32_t  pthread_once_t;
typedef int32_t  pthread_rwlock_t;
typedef int32_t  pthread_rwlockattr_t;
typedef int32_t  pthread_spinlock_t;
typedef int32_t  pthread_barrier_t;
typedef int32_t  pthread_barrierattr_t;

// BSD/Legacy
typedef uint32_t vm_offset_t;
typedef uint32_t vm_size_t;

#endif // HOST_TEST

#endif // _SYS_TYPES_H
