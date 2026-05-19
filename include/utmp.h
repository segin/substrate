/*
 * <utmp.h> — login records.
 *
 * Substrate follows the glibc layout so off-the-shelf utilities
 * (who, last, w) port without surgery.  utmp tracks current logins
 * at /var/run/utmp; wtmp keeps a chronological history at
 * /var/log/wtmp.  Each is a sequence of fixed-size struct utmp
 * records; readers index by either ut_line (terminal device, e.g.
 * "tty1" or "pts/0") or ut_id (a 4-char abbreviation).
 *
 * Functions:
 *   setutent / endutent / getutent — sequential scan
 *   getutid / getutline             — match by id / line
 *   pututline                       — insert or update one entry
 *   utmpname                        — point library at a different
 *                                      utmp file
 *   logwtmp / updwtmp               — append-only writes to wtmp
 */
#ifndef _UTMP_H
#define _UTMP_H

#include <sys/types.h>
#include <sys/time.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UT_LINESIZE  32
#define UT_NAMESIZE  32
#define UT_HOSTSIZE  256

/* ut_type values */
#define EMPTY          0
#define RUN_LVL        1
#define BOOT_TIME      2
#define NEW_TIME       3
#define OLD_TIME       4
#define INIT_PROCESS   5
#define LOGIN_PROCESS  6
#define USER_PROCESS   7
#define DEAD_PROCESS   8
#define ACCOUNTING     9

struct exit_status {
    int32_t e_termination;   /* process termination signal */
    int32_t e_exit;          /* process exit status */
};

struct utmp {
    short              ut_type;             /* type of record */
    pid_t              ut_pid;              /* PID of login process */
    char               ut_line[UT_LINESIZE];/* device name, no /dev/ */
    char               ut_id[4];            /* /etc/inittab id */
    char               ut_user[UT_NAMESIZE];/* username */
    char               ut_host[UT_HOSTSIZE];/* remote host */
    struct exit_status ut_exit;             /* exit status (DEAD_PROCESS) */
    int32_t            ut_session;          /* session id */
    struct {
        int32_t tv_sec;
        int32_t tv_usec;
    } ut_tv;                                /* time entry was made */
    int32_t            ut_addr_v6[4];       /* IP of remote host (v6) */
    char               __reserved[20];
};

/* glibc-compat aliases */
#define ut_name ut_user
#define ut_time ut_tv.tv_sec
#define ut_addr ut_addr_v6[0]

#define UTMP_FILE      "/var/run/utmp"
#define WTMP_FILE      "/var/log/wtmp"
#define UTMP_FILENAME  UTMP_FILE
#define WTMP_FILENAME  WTMP_FILE
#define _PATH_UTMP     UTMP_FILE
#define _PATH_WTMP     WTMP_FILE

void          setutent(void);
void          endutent(void);
struct utmp  *getutent(void);
struct utmp  *getutid(const struct utmp *ut);
struct utmp  *getutline(const struct utmp *ut);
struct utmp  *pututline(const struct utmp *ut);
int           utmpname(const char *file);

/* Append-only writers for wtmp.  logwtmp() is the convenience
 * front-door: it composes a USER_PROCESS / DEAD_PROCESS entry from
 * the line+name+host triple (a dead entry is signalled by name=""). */
void          updwtmp(const char *wtmp_file, const struct utmp *ut);
void          logwtmp(const char *line, const char *name, const char *host);

#ifdef __cplusplus
}
#endif

#endif /* _UTMP_H */
