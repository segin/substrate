/*
 * <utmpx.h> — POSIX "extended" utmp.
 *
 * POSIX standardised the legacy <utmp.h> API under a new header
 * with a struct utmpx alias and the *x variants of every function.
 * In practice substrate's struct utmp is already the modern wide
 * form (32-byte line/user, IPv6-capable ut_addr_v6, etc.), so this
 * header is a thin alias over <utmp.h> for POSIX-only callers.
 */
#ifndef _UTMPX_H
#define _UTMPX_H

#include <utmp.h>

#ifdef __cplusplus
extern "C" {
#endif

#define utmpx          utmp
#define _PATH_UTMPX    _PATH_UTMP
#define _PATH_WTMPX    _PATH_WTMP

#define setutxent      setutent
#define endutxent      endutent
#define getutxent      getutent
#define getutxid       getutid
#define getutxline     getutline
#define pututxline     pututline

#ifdef __cplusplus
}
#endif

#endif /* _UTMPX_H */
