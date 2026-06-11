/*
 * lib/sys/sysret.h — internal helper for translating the raw syscall
 * return value into the POSIX -1 + errno error contract.
 *
 * The native raw syscall() returns the kernel value directly: on error
 * it returns a NEGATIVE errno in the range [-4095, -1] (it does NOT set
 * errno or return -1).  Every typed wrapper that exposes a public
 * POSIX/Linux function with a -1 + errno error contract must run its
 * raw return through __sysret() (or one of the typed variants below).
 *
 * This header is internal to lib/sys/; it is not installed.
 */
#ifndef _LIBSYS_SYSRET_H
#define _LIBSYS_SYSRET_H

#include <errno.h>

/* Generic translator: maps a negative-errno raw return to -1 + errno. */
static inline long __sysret(long r) {
    if (r < 0 && r >= -4095) {
        errno = (int)-r;
        return -1;
    }
    return r;
}

#endif /* _LIBSYS_SYSRET_H */
