#ifdef HOST_TEST
#include_next <sys/types.h>
#include <stdint.h>
#include <stddef.h>
#ifndef _TID_T_DECLARED
#define _TID_T_DECLARED
typedef int32_t tid_t;
#endif
#else
#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H

#include <stdint.h>
#include <stddef.h>

typedef int32_t pid_t;
typedef int32_t tid_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef int32_t register_t;

typedef int64_t off_t;
typedef int64_t time_t;

#ifndef HOST_TEST
typedef long fpos_t;
#endif

typedef int32_t mode_t;
typedef uint32_t dev_t;
typedef uint64_t ino_t;
typedef uint32_t nlink_t;
typedef uint32_t blksize_t;
typedef int64_t blkcnt_t;

#ifndef HOST_TEST
typedef int32_t ssize_t;
#endif
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

#endif
#endif
