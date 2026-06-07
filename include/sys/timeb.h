/*
 * <sys/timeb.h> — obsolete ftime(3).  Provided for legacy code (CDE's dtcm
 * getdate) that still includes it; new code uses gettimeofday/clock_gettime.
 */
#ifndef _SYS_TIMEB_H
#define _SYS_TIMEB_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct timeb {
    time_t         time;      /* seconds since the Epoch */
    unsigned short millitm;   /* milliseconds */
    short          timezone;  /* minutes west of CUT */
    short          dstflag;   /* nonzero if DST in effect */
};

int ftime(struct timeb *tp);

#ifdef __cplusplus
}
#endif
#endif /* _SYS_TIMEB_H */
