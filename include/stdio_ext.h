#ifndef _STDIO_EXT_H
#define _STDIO_EXT_H

/*
 * <stdio_ext.h> -- stdio extensions that reach into FILE state.
 *
 * Only __fseterr() is provided so far.  gnulib's fseterr module looks for it
 * first; finding nothing, it compiles fseterr.c, which knows the FILE layout
 * of a list of other libcs and otherwise stops the build:
 *
 *     fseterr.c:78: #error "Please port gnulib fseterr.c to your platform!"
 *
 * nano 9.2 and texinfo 7.3 both hit that.  With __fseterr() in libc,
 * configure defines HAVE___FSETERR and fseterr.c is never built.
 *
 * The rest of the glibc/Solaris family (__freading, __fpending, __fpurge,
 * __freadahead, ...) is not implemented.  gnulib checks each function
 * separately, so providing this one does not make configure assume the
 * others exist.
 *
 * Self-contained like <wchar.h>: FILE is forward-declared under the guard
 * <stdio.h> shares, rather than included, so this header can be reached from
 * anywhere without joining an include cycle.
 */

#ifndef __FILE_defined
#define __FILE_defined 1
typedef struct FILE FILE;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Set the error indicator of STREAM, as a failed write would.  clearerr()
 * resets it; ferror() reports it. */
void __fseterr(FILE *stream);

#ifdef __cplusplus
}
#endif

#endif /* _STDIO_EXT_H */
