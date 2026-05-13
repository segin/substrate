#ifndef _SUBSTRATE_SYS_TYPES_H
#define _SUBSTRATE_SYS_TYPES_H

#ifdef HOST_TEST
// For host builds: use system types and add only Substrate-specific extensions
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include_next <sys/types.h>
#include <stdint.h>
#include <stddef.h>

#if defined(__x86_64__) || defined(_M_X64)
#define SUBSTRATE_PID_MAX 9999999
#define SUBSTRATE_TID_MAX 9999999
#else
#define SUBSTRATE_PID_MAX 99999
#define SUBSTRATE_TID_MAX 99999
#endif

// Substrate-specific kernel types (not in POSIX)
typedef uint32_t kdev_t;
typedef uint32_t vm_offset_t;
typedef uint32_t vm_size_t;
typedef int32_t  tid_t;

#else
// Substrate Native Types
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

/* Pthread types live in <pthread.h>; this header used to declare
 * them as opaque int32_t which conflicts with the real struct
 * shape (pthread_cond_t carries a futex-backed seq counter, etc.).
 * Code that needs the types should #include <pthread.h>. */
typedef int32_t  pthread_spinlock_t;
typedef int32_t  pthread_barrier_t;
typedef int32_t  pthread_barrierattr_t;

// BSD/Legacy
typedef uint32_t vm_offset_t;
typedef uint32_t vm_size_t;

#endif // HOST_TEST

#endif // _SUBSTRATE_SYS_TYPES_H
