/*
 * sys/abi32.h - field types for structures shared with 32-bit processes
 *
 * Every process today is a 32-bit (i386 ABI) one, on the i386 kernel and,
 * in compatibility mode, on the x86_64 kernel (docs/specs/abi-amd64.md,
 * section 10).  The structures they exchange with the kernel by value --
 * struct stat, timespec, rusage, statfs and the like -- therefore keep the
 * i386 layout on both kernels, and their fields are declared with these
 * types to make that hold by construction:
 *
 *   abi_long_t / abi_ulong_t     a C long of the i386 ABI: 32 bits
 *   abi_size_t                   a size_t of the i386 ABI: 32 bits
 *   abi_int64_t / abi_uint64_t   a 64-bit field aligned as the i386 ABI
 *                                aligns one in a structure: to 4 bytes
 *   uptr32_t                     a user pointer as the process stores it
 *
 * The same types describe the process's side of a system call argument: a
 * handler whose parameter is the user's long, size_t or pointer slot
 * declares it with them rather than with the native type.
 *
 * On the i386 kernel they are exactly the types they replace.  On the
 * x86_64 kernel an LP64 long would be 64 bits and a 64-bit field would be
 * aligned to 8, moving every field after it.
 *
 * Structures that carry pointers cannot be fixed this way and are
 * translated where they cross the boundary (<sys/compat32.h>).
 */
#ifndef _SYS_ABI32_H
#define _SYS_ABI32_H

#include <stddef.h>
#include <stdint.h>

#ifdef SUBSTRATE_ARCH_X86_64
typedef int32_t  abi_long_t;
typedef uint32_t abi_ulong_t;
typedef uint32_t abi_size_t;
typedef int64_t  abi_int64_t  __attribute__((aligned(4)));
typedef uint64_t abi_uint64_t __attribute__((aligned(4)));
#else
typedef long          abi_long_t;
typedef unsigned long abi_ulong_t;
typedef size_t        abi_size_t;
typedef int64_t       abi_int64_t;
typedef uint64_t      abi_uint64_t;
#endif

/* A pointer as the process stores it: its 32-bit address. */
typedef uint32_t uptr32_t;

/* A user pointer value, as the kernel can hold it. */
#define UPTR32(p) ((void *)(uintptr_t)(uptr32_t)(p))

/*
 * Pin a structure to its i386 size on either kernel, so a field that
 * escapes these types -- or a second, native definition of the structure
 * winning an include guard -- fails the build instead of every 32-bit
 * process.  Host test builds compile these headers with the host's own
 * layout and are exempt.
 */
#if defined(__i386__) || defined(SUBSTRATE_ARCH_X86_64)
#define ABI32_ASSERT_SIZE(type, size) \
    _Static_assert(sizeof(type) == (size), #type " must keep its i386 size")
#else
#define ABI32_ASSERT_SIZE(type, size) \
    _Static_assert(1, "host builds use the host layout")
#endif

#endif /* _SYS_ABI32_H */
