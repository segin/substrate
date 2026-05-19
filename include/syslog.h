/*
 * <syslog.h> — system log API (RFC 5424 / 4.4BSD / POSIX.1-2017).
 *
 * Daemons announce themselves with openlog(), then call syslog()
 * with a priority (facility | level) and printf-style args.  Output
 * goes to /dev/log (a UNIX-domain datagram socket the system
 * syslogd consumes) — substrate doesn't have a syslogd yet, so the
 * libc implementation buffers to /var/log/messages when /dev/log
 * isn't present, and silently drops if neither path exists.
 *
 * Facility + level constants follow the standard layout: facility
 * in the upper bits, level in the lower three.  Macros LOG_PRI /
 * LOG_FAC unpack; LOG_MAKEPRI / LOG_MASK / LOG_UPTO compose.
 */
#ifndef _SYSLOG_H
#define _SYSLOG_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Levels (severity) — lower number = more severe */
#define LOG_EMERG    0   /* system is unusable */
#define LOG_ALERT    1   /* action must be taken immediately */
#define LOG_CRIT     2   /* critical conditions */
#define LOG_ERR      3   /* error conditions */
#define LOG_WARNING  4   /* warning conditions */
#define LOG_NOTICE   5   /* normal but significant condition */
#define LOG_INFO     6   /* informational */
#define LOG_DEBUG    7   /* debug-level messages */

#define LOG_PRIMASK  0x07
#define LOG_PRI(p)   ((p) & LOG_PRIMASK)
#define LOG_MAKEPRI(fac, pri)  (((fac) << 3) | (pri))
#define LOG_MASK(pri)          (1 << (pri))
#define LOG_UPTO(pri)          ((1 << ((pri) + 1)) - 1)

/* Facilities */
#define LOG_KERN     (0  << 3)
#define LOG_USER     (1  << 3)
#define LOG_MAIL     (2  << 3)
#define LOG_DAEMON   (3  << 3)
#define LOG_AUTH     (4  << 3)
#define LOG_SYSLOG   (5  << 3)
#define LOG_LPR      (6  << 3)
#define LOG_NEWS     (7  << 3)
#define LOG_UUCP     (8  << 3)
#define LOG_CRON     (9  << 3)
#define LOG_AUTHPRIV (10 << 3)
#define LOG_FTP      (11 << 3)
#define LOG_LOCAL0   (16 << 3)
#define LOG_LOCAL1   (17 << 3)
#define LOG_LOCAL2   (18 << 3)
#define LOG_LOCAL3   (19 << 3)
#define LOG_LOCAL4   (20 << 3)
#define LOG_LOCAL5   (21 << 3)
#define LOG_LOCAL6   (22 << 3)
#define LOG_LOCAL7   (23 << 3)

#define LOG_NFACILITIES 24
#define LOG_FACMASK     0x03f8
#define LOG_FAC(p)      (((p) & LOG_FACMASK) >> 3)

/* openlog() option bits */
#define LOG_PID      0x01   /* include PID in each message */
#define LOG_CONS     0x02   /* fall back to /dev/console on /dev/log failure */
#define LOG_ODELAY   0x04   /* delay open until first syslog() */
#define LOG_NDELAY   0x08   /* open immediately */
#define LOG_NOWAIT   0x10   /* don't wait for child (deprecated) */
#define LOG_PERROR   0x20   /* also write to stderr */

void   openlog(const char *ident, int option, int facility);
void   closelog(void);
int    setlogmask(int mask);
void   syslog(int priority, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void   vsyslog(int priority, const char *fmt, va_list ap);

#ifdef __cplusplus
}
#endif

#endif /* _SYSLOG_H */
