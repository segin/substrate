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
#define HOST_NAME_MAX 64
#define LOGIN_NAME_MAX 256
#define IOV_MAX 1024
#define LINE_MAX 2048
/* POSIX hint for the maximum number of file descriptors a process
 * can have open.  Substrate's actual limit is set per-process by
 * RLIMIT_NOFILE at runtime; OPEN_MAX is the compile-time constant
 * POSIX-conformant code expects to find. */
#define OPEN_MAX 1024

/* SSIZE_MAX — maximum value that fits in ssize_t.  ssize_t mirrors
 * long on substrate (4 bytes on i386, 8 on x86_64). */
#if __SIZEOF_LONG__ == 8
#define SSIZE_MAX 9223372036854775807L
#else
#define SSIZE_MAX 2147483647L
#endif

#endif
