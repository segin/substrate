#ifndef _SYS_DIR_H
#define _SYS_DIR_H 1

/*
 * Historical BSD directory header.  Modern code uses <dirent.h>; this
 * compatibility shim (matching glibc's <sys/dir.h>) exists for old sources
 * that still spell the directory entry `struct direct` and reach for
 * MAXNAMLEN.  `struct direct` is just the historical name for the POSIX
 * `struct dirent`.
 */

#include <dirent.h>

#ifndef MAXNAMLEN
#define MAXNAMLEN 255
#endif

#ifndef direct
#define direct dirent
#endif

#endif /* _SYS_DIR_H */
