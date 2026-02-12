#ifndef _LIBGEN_H
#define _LIBGEN_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * POSIX.1-2008
 *
 * The basename() function shall return a pointer to the final component of
 * the pathname pointed to by path.
 *
 * The dirname() function shall return a pointer to the parent directory of
 * the pathname pointed to by path.
 *
 * Both functions may modify the string pointed to by path, and may return
 * a pointer to static storage that may then be overwritten by a subsequent
 * call to basename() or dirname().
 */

char *basename(char *path);
char *dirname(char *path);

#ifdef __cplusplus
}
#endif

#endif /* _LIBGEN_H */
