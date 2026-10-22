#ifndef _STDIO_EXT_H
#define _STDIO_EXT_H

/*
 * <stdio_ext.h> -- stdio extensions that reach into FILE state.
 *
 * gnulib looks for each of these first; finding one missing, it compiles a
 * replacement that knows the FILE layout of a list of other libcs and
 * otherwise stops the build:
 *
 *     fseterr.c:78: #error "Please port gnulib fseterr.c to your platform!"
 *
 * nano 9.2 and texinfo 7.3 hit that for __fseterr, and a native build of
 * any gnulib package for __fpurge and __freading.  With them in libc,
 * configure defines HAVE___FSETERR and the rest, and the replacements are
 * never built.
 *
 * __freadahead, __freadptr, __fbufsize, __flbf and the other members of the
 * glibc/Solaris family are not implemented.  gnulib checks each function
 * separately, so providing some does not make configure assume the others
 * exist.
 *
 * Self-contained like <wchar.h>: FILE is forward-declared under the guard
 * <stdio.h> shares, rather than included, so this header can be reached from
 * anywhere without joining an include cycle.
 */

#ifndef __FILE_defined
#define __FILE_defined 1
typedef struct FILE FILE;
#endif

#include <stddef.h>     /* size_t; the compiler's own, so no cycle */

#ifdef __cplusplus
extern "C" {
#endif

/* Set the error indicator of STREAM, as a failed write would.  clearerr()
 * resets it; ferror() reports it. */
void __fseterr(FILE *stream);

/* Discard what STREAM has buffered -- unwritten output, read-ahead and a
 * pushed-back character -- without writing it or moving the descriptor. */
void __fpurge(FILE *stream);

/* Non-zero if STREAM is read-only, or its last operation was a read. */
int __freading(FILE *stream);

/* Non-zero if STREAM is write-only, or its last operation was a write. */
int __fwriting(FILE *stream);

/* The number of bytes STREAM holds that are not yet written. */
size_t __fpending(FILE *stream);

#ifdef __cplusplus
}
#endif

#endif /* _STDIO_EXT_H */
