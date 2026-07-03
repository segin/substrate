#ifndef _LIMITS_H
#define _LIMITS_H

#define CHAR_BIT 8

#define SCHAR_MIN (-128)
#define SCHAR_MAX 127
#define UCHAR_MAX 255

#define SHRT_MIN (-32768)
#define SHRT_MAX 32767
#define USHRT_MAX 65535

#define INT_MIN (-2147483647 - 1)
#define INT_MAX 2147483647
#define UINT_MAX 4294967295U

#ifdef __x86_64__
#define LONG_MIN (-9223372036854775807L - 1L)
#define LONG_MAX 9223372036854775807L
#define ULONG_MAX 18446744073709551615UL
#else
#define LONG_MIN (-2147483647L - 1L)
#define LONG_MAX 2147483647L
#define ULONG_MAX 4294967295UL
/* Max supplementary group IDs. */
#ifndef NGROUPS_MAX
#define NGROUPS_MAX 32
#endif
#endif

#define LLONG_MIN (-9223372036854775807LL - 1LL)
#define LLONG_MAX 9223372036854775807LL
#define ULLONG_MAX 18446744073709551615ULL

#define PATH_MAX 4096
#define NAME_MAX 255
/* POSIX message-queue priority ceiling: valid msg_prio is 0 .. MQ_PRIO_MAX-1.
 * Matches the in-kernel limit (sys/kern/posix_mqueue.c) and <mqueue.h>. */
#ifndef MQ_PRIO_MAX
#define MQ_PRIO_MAX 64
#endif
#define HOST_NAME_MAX 64
#define LOGIN_NAME_MAX 256
#define IOV_MAX 1024
#define LINE_MAX 2048
/* Maximum number of POSIX semaphores a single process may have (sem_init /
 * sem_open).  POSIX requires this to be at least _POSIX_SEM_NSEMS_MAX (256);
 * sem_init()/sem_open() report ENOSPC once it is reached (sem_init/7-1). */
#define SEM_NSEMS_MAX 256
/* POSIX hint for the maximum number of file descriptors a process
 * can have open.  Substrate's actual limit is set per-process by
 * RLIMIT_NOFILE at runtime; OPEN_MAX is the compile-time constant
 * POSIX-conformant code expects to find. */
#define OPEN_MAX 1024

/* AIO_MAX — maximum number of outstanding asynchronous I/O operations.
 * librt's worker-pool AIO enforces this: aio_read()/aio_write()/lio_listio()
 * fail with EAGAIN once this many requests are outstanding (submitted but
 * not yet reaped by aio_return()).  sysconf(_SC_AIO_MAX) reports the same
 * value.  Chosen well above the largest concurrent-AIO batch substrate's
 * own tests submit and comfortably below any single lio_listio ceiling. */
#ifndef AIO_MAX
#define AIO_MAX 256
#endif

/* SSIZE_MAX — maximum value that fits in ssize_t.  ssize_t mirrors
 * long on substrate (4 bytes on i386, 8 on x86_64). */
#if __SIZEOF_LONG__ == 8
#define SSIZE_MAX 9223372036854775807L
#else
#define SSIZE_MAX 2147483647L
#endif

/* PTHREAD_STACK_MIN — minimum size in bytes of a thread stack.  POSIX
 * places this in <limits.h>; <pthread.h> mirrors it.  libpthread creates
 * every thread with a fixed 64 KiB stack (pthread_attr_setstacksize is
 * advisory), so this is the real per-thread minimum on substrate. */
#ifndef PTHREAD_STACK_MIN
#define PTHREAD_STACK_MIN 65536
#endif

/* POSIX minimum-acceptable values (the guaranteed floors, distinct from the
 * actual runtime limits queried via sysconf/pathconf). */
#ifndef _POSIX_ARG_MAX
#define _POSIX_ARG_MAX     4096
#define _POSIX_CHILD_MAX   25
#define _POSIX_LINK_MAX    8
#define _POSIX_MAX_CANON   255
#define _POSIX_MAX_INPUT   255
#define _POSIX_NAME_MAX    14
#define _POSIX_NGROUPS_MAX 8
#define _POSIX_OPEN_MAX    20
#define _POSIX_PATH_MAX    256
#define _POSIX_PIPE_BUF    512
#define _POSIX_SSIZE_MAX   32767
#define _POSIX_STREAM_MAX  8
#define _POSIX_TZNAME_MAX  6
#define _POSIX_AIO_MAX     1
#define _POSIX_AIO_LISTIO_MAX 2
#endif

#endif
