#ifndef _LASTLOG_H
#define _LASTLOG_H

/*
 * <lastlog.h> — the per-user "last login" record stored in
 * _PATH_LASTLOG (/var/log/lastlog), indexed by uid.  Historically some
 * systems declared this in <utmp.h>; glibc and the BSDs split it out.
 * Consumers (e.g. TDE's tdm sessreg) write a record with lseek+write.
 */

#include <stdint.h>
#include <utmp.h>

struct lastlog {
    int32_t ll_time;
    char    ll_line[UT_LINESIZE];
    char    ll_host[UT_HOSTSIZE];
};

#endif /* _LASTLOG_H */
