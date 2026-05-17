/*
 * <sys/param.h> — system parameter constants.
 *
 * BSD-derived umbrella header that exposes implementation limits,
 * MIN/MAX/MAXPATHLEN/MAXBSIZE, page-size hints, plus the BSD release
 * detection macro family.  Ports of BSD utilities reach for this
 * header before any of <limits.h> / <unistd.h>, so substrate ships
 * it as a thin facade over those.
 */
#ifndef _SYS_PARAM_H
#define _SYS_PARAM_H

#include <sys/types.h>
#include <limits.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

/* Path / filename limits.  POSIX guarantees these via <limits.h>. */
#ifndef MAXPATHLEN
#define MAXPATHLEN  PATH_MAX
#endif
#ifndef MAXNAMLEN
#define MAXNAMLEN   NAME_MAX
#endif
#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 256
#endif
#ifndef MAXSYMLINKS
#define MAXSYMLINKS 32
#endif
#ifndef MAXLOGNAME
#define MAXLOGNAME  17
#endif

/* Substrate page size.  i386 4K everywhere.  */
#ifndef PAGE_SIZE
#define PAGE_SIZE   4096
#endif
#ifndef PAGE_SHIFT
#define PAGE_SHIFT  12
#endif
#ifndef PAGE_MASK
#define PAGE_MASK   (PAGE_SIZE - 1)
#endif

/* I/O buffer sizing (BSD heritage).  */
#ifndef DEV_BSIZE
#define DEV_BSIZE   512
#endif
#ifndef MAXBSIZE
#define MAXBSIZE    65536
#endif
#ifndef NBBY
#define NBBY        8       /* number of bits per byte */
#endif

/* MIN/MAX/howmany/roundup utility macros — BSD originals.  */
#ifndef MIN
#define MIN(a, b)   (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b)   (((a) > (b)) ? (a) : (b))
#endif
#ifndef howmany
#define howmany(x, y)   (((x) + ((y) - 1)) / (y))
#endif
#ifndef roundup
#define roundup(x, y)   ((((x) + ((y) - 1)) / (y)) * (y))
#endif
#ifndef rounddown
#define rounddown(x, y) (((x) / (y)) * (y))
#endif
#ifndef powerof2
#define powerof2(x)     ((((x) - 1) & (x)) == 0)
#endif

/* BSD release marker, useful for #if guards in BSD-style ports.  */
#define BSD                 199506
#define BSD4_3              1
#define BSD4_4              1

#endif /* _SYS_PARAM_H */
