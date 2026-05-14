/*
 * <glob.h> — pathname pattern expansion.
 *
 * Substrate libc provides POSIX glob(3) and globfree(3).  Used by
 * GNU make's $(wildcard ...), by shells expanding wildcards in
 * command arguments, and by anything that wants to convert a
 * pattern like "*.c" or shell wildcards into the matching pathnames.
 *
 * The match engine is fnmatch(3) — substrate's fnmatch handles
 * `*`, `?`, `[abc]`, `[!abc]`, `[a-z]` and the FNM_PATHNAME /
 * FNM_PERIOD flags.  glob's path-component handling layers on
 * top: split the pattern at slashes, fnmatch each component
 * against the corresponding directory entries, recurse on
 * matching sub-directories.
 */

#ifndef _GLOB_H
#define _GLOB_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t   gl_pathc;     /* count of paths matched so far */
    char   **gl_pathv;     /* matched pathnames (gl_pathc + gl_offs + 1 ptrs incl. NULL) */
    size_t   gl_offs;      /* slots reserved at the front of gl_pathv */
} glob_t;

/* Flags passed to glob(). */
#define GLOB_APPEND     (1 <<  0)   /* append to existing gl_pathv */
#define GLOB_DOOFFS     (1 <<  1)   /* reserve gl_offs entries at the head */
#define GLOB_ERR        (1 <<  2)   /* return on read error */
#define GLOB_MARK       (1 <<  3)   /* append `/` to directory matches */
#define GLOB_NOCHECK    (1 <<  4)   /* return the pattern itself when nothing matched */
#define GLOB_NOSORT     (1 <<  5)   /* don't sort results */
#define GLOB_NOESCAPE   (1 <<  6)   /* disable backslash escaping in pattern */
#define GLOB_PERIOD     (1 <<  7)   /* allow leading-period matches by `*` etc. */

/* Return codes. */
#define GLOB_NOSPACE    1
#define GLOB_ABORTED    2
#define GLOB_NOMATCH    3
#define GLOB_NOSYS      4

int  glob(const char *pattern, int flags,
          int (*errfunc)(const char *epath, int eerrno),
          glob_t *pglob);
void globfree(glob_t *pglob);

#ifdef __cplusplus
}
#endif

#endif /* _GLOB_H */
