/*
 * <err.h> — BSD-style error reporting helpers.
 *
 * Format-string + strerror(errno) wrappers that the BSD source
 * tradition uses everywhere — every other line of an OpenBSD
 * userland program calls warn() or err().  Substrate adopts the
 * same API so straight ports compile without rewriting their
 * diagnostic calls.
 *
 *   err(int exit_status, const char *fmt, ...) noreturn
 *   warn(const char *fmt, ...)
 *   errx(int exit_status, const char *fmt, ...) noreturn   — no errno
 *   warnx(const char *fmt, ...)                            — no errno
 *
 * All write to stderr, prefixed with the program name (from
 * setprogname() / __progname).
 */
#ifndef _ERR_H
#define _ERR_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

void err   (int eval, const char *fmt, ...) __attribute__((noreturn, format(printf, 2, 3)));
void verr  (int eval, const char *fmt, va_list ap) __attribute__((noreturn));
void errx  (int eval, const char *fmt, ...) __attribute__((noreturn, format(printf, 2, 3)));
void verrx (int eval, const char *fmt, va_list ap) __attribute__((noreturn));

void warn  (const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void vwarn (const char *fmt, va_list ap);
void warnx (const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void vwarnx(const char *fmt, va_list ap);

#ifdef __cplusplus
}
#endif

#endif /* _ERR_H */
