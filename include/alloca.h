/*
 * <alloca.h> — stack-frame-scoped allocator.
 *
 * Returns a pointer to (uninitialised) memory of `size` bytes that
 * is released automatically when the enclosing function returns.
 * Strictly a compiler intrinsic — every modern GCC and Clang
 * implements it via `__builtin_alloca`, which the compiler turns
 * into a single `sub %esp` (or equivalent) at the call site.  Not a
 * libc function, not a syscall, no kfree needed.
 *
 * Substrate ships this header for source compatibility with code
 * that does `#include <alloca.h>` (gnulib, GNU make, glob.c in
 * busybox, libpthread internals, ...).  Some BSD-flavoured code
 * expects alloca() to be visible through <stdlib.h>; we re-expose
 * it there too.
 */
#ifndef _ALLOCA_H
#define _ALLOCA_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#undef  alloca
#define alloca(size)   __builtin_alloca(size)

#ifdef __cplusplus
}
#endif

#endif /* _ALLOCA_H */
