/*
 * <wordexp.h> — perform word expansion like a POSIX shell.
 *
 * Substrate libc provides POSIX wordexp(3) and wordfree(3).  wordexp()
 * takes a string and performs the word expansions a shell applies to a
 * command line — tilde expansion, parameter ($VAR / ${VAR}) expansion,
 * field splitting on IFS, and pathname (glob) expansion — returning the
 * resulting NULL-terminated word vector.
 *
 * The expansion layers over substrate's glob(3) for the pathname step and
 * getpwnam(3) for `~user` tilde expansion.  Command substitution (backticks
 * and `$(...)`) and arithmetic expansion (`$((...))`) are not performed:
 * wordexp() reports WRDE_CMDSUB if it encounters command substitution, as a
 * shell would when WRDE_NOCMD is requested.
 */

#ifndef _WORDEXP_H
#define _WORDEXP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t   we_wordc;     /* count of expanded words */
    char   **we_wordv;     /* the expanded words (we_wordc + we_offs + 1 ptrs, NULL-terminated) */
    size_t   we_offs;      /* slots reserved at the front of we_wordv (WRDE_DOOFFS) */
} wordexp_t;

/* Flags for wordexp(). */
#define WRDE_DOOFFS   (1 << 0)   /* reserve we_offs NULL slots at the head of we_wordv */
#define WRDE_APPEND   (1 << 1)   /* append words to an existing we_wordv */
#define WRDE_NOCMD    (1 << 2)   /* fail (WRDE_CMDSUB) on command substitution */
#define WRDE_REUSE    (1 << 3)   /* reuse/free a we_wordv from a previous call */
#define WRDE_SHOWERR  (1 << 4)   /* do not redirect shell error output (no-op here) */
#define WRDE_UNDEF    (1 << 5)   /* fail (WRDE_BADVAL) on expansion of an unset variable */

/* Error return codes. */
#define WRDE_NOSPACE   1   /* out of memory */
#define WRDE_BADCHAR   2   /* unquoted shell metacharacter ( | & ; < > ( ) { } ) */
#define WRDE_BADVAL    3   /* undefined variable referenced with WRDE_UNDEF */
#define WRDE_CMDSUB    4   /* command substitution requested/encountered */
#define WRDE_SYNTAX    5   /* shell syntax error (e.g. unbalanced quotes) */

int  wordexp(const char *words, wordexp_t *we, int flags);
void wordfree(wordexp_t *we);

#ifdef __cplusplus
}
#endif

#endif /* _WORDEXP_H */
