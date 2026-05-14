/*
 * <utime.h> — legacy file-time setter.  Superseded by
 * utimensat / utimes; kept for source-compat.
 */

#ifndef _UTIME_H
#define _UTIME_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct utimbuf {
    time_t actime;     /* last access time */
    time_t modtime;    /* last modification time */
};

int utime(const char *path, const struct utimbuf *times);

#ifdef __cplusplus
}
#endif
#endif
