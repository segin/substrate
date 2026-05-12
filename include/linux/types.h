/*
 * <linux/types.h> — Linux UAPI typedefs.  Substrate isn't Linux but
 * autoconf probes for this header in libstdc++ etc.  Provide just
 * enough that includers compile; real Linux UAPI is via the
 * personality emulation layer for Linux-personality binaries, not
 * the substrate-native headers.
 */
#ifndef _LINUX_TYPES_H
#define _LINUX_TYPES_H
#include <stdint.h>
typedef uint8_t  __u8;
typedef int8_t   __s8;
typedef uint16_t __u16;
typedef int16_t  __s16;
typedef uint32_t __u32;
typedef int32_t  __s32;
typedef uint64_t __u64;
typedef int64_t  __s64;
typedef unsigned long  __kernel_ulong_t;
typedef long           __kernel_long_t;
typedef int            __kernel_pid_t;
typedef unsigned int   __kernel_uid_t;
typedef unsigned int   __kernel_gid_t;
typedef long           __kernel_off_t;
typedef long long      __kernel_loff_t;
typedef long           __kernel_time_t;
typedef long           __kernel_clock_t;
#endif
