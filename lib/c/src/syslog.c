/*
 * lib/c/src/syslog.c — libc client side of the system log API.
 *
 * Format per RFC 3164 (the BSD syslog protocol — what every existing
 * syslogd parses), sent over /dev/log (UNIX-domain datagram socket
 * the daemon listens on).  If /dev/log isn't there yet (early boot,
 * no syslogd), and LOG_CONS is set, fall back to /dev/console.
 * LOG_PERROR additionally echoes to stderr.
 *
 *   <PRI>TIMESTAMP HOSTNAME ident[pid]: message
 *
 * PRI is the encoded facility|level integer in angle brackets.
 *
 * The socket is opened lazily on the first syslog() call (LOG_ODELAY
 * is the default per POSIX), unless LOG_NDELAY was passed to
 * openlog().  Resends after a closelog() reopen lazily again.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>

/* BSD convention.  Substrate's /dev is a kernel-managed devfs that
 * can't host an arbitrary AF_UNIX socket inode (no backing storage),
 * so we use /var/run/log — the path FreeBSD's syslogd has used
 * forever.  Linux/glibc uses /dev/log instead; we try that path too
 * as a fallback so binaries that hard-code it (gnulib's syslog
 * module, sendmail, ...) don't silently lose messages. */
#define SYSLOG_SOCK_PATH      "/var/run/log"
#define SYSLOG_SOCK_PATH_ALT  "/dev/log"
#define SYSLOG_FALLBACK       "/dev/console"

static int          g_logfd       = -1;
static const char  *g_ident       = NULL;
static int          g_option      = 0;
static int          g_facility    = LOG_USER;
static int          g_mask        = 0xff;   /* allow all by default */

/* Substrate's AF_UNIX implementation is currently SOCK_STREAM-only.
 * We frame messages with a trailing newline so the daemon side can
 * cheaply parse them (it does `until '\n'` over the recv buffer).
 * BSD/Linux syslog historically used SOCK_DGRAM, but RFC 6587
 * (syslog over TCP) standardises octet-stuffing for stream
 * transports; we use the simpler non-transparent newline framing
 * because we control both ends.  */
static int try_connect(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un sun;
    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    strlcpy(sun.sun_path, path, sizeof(sun.sun_path));
    if (connect(fd, (struct sockaddr *)&sun, sizeof(sun)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void try_open(void)
{
    if (g_logfd >= 0) return;
    g_logfd = try_connect(SYSLOG_SOCK_PATH);
    if (g_logfd < 0) g_logfd = try_connect(SYSLOG_SOCK_PATH_ALT);
}

void openlog(const char *ident, int option, int facility)
{
    g_ident    = ident;
    g_option   = option;
    g_facility = facility ? facility : LOG_USER;
    if (option & LOG_NDELAY) try_open();
}

void closelog(void)
{
    if (g_logfd >= 0) { close(g_logfd); g_logfd = -1; }
    g_ident = NULL;
}

int setlogmask(int mask)
{
    int prev = g_mask;
    if (mask) g_mask = mask;
    return prev;
}

void vsyslog(int priority, const char *fmt, va_list ap)
{
    /* Drop levels not in the mask. */
    if (!(g_mask & LOG_MASK(LOG_PRI(priority)))) return;

    /* If caller didn't bake a facility into priority, use ours. */
    if ((priority & LOG_FACMASK) == 0) priority |= g_facility;

    /* RFC 3164: <PRI>Mmm DD HH:MM:SS HOSTNAME ident[pid]: msg
     * Hostname is left blank when not known (kernel route does the
     * fixup); ident defaults to argv[0]'s basename if openlog
     * wasn't called. */
    char  buf[1024];
    int   n = 0;
    /* snprintf returns the length it WOULD have written, so n can run past
     * sizeof(buf); left unclamped, the next `buf + n` / `sizeof(buf) - n`
     * pair writes out of bounds (the size underflows to a huge size_t).
     * Clamp after every append so the cursor stays inside the buffer. */
#define SYSLOG_CLAMP() do { \
        if (n < 0 || n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1; \
    } while (0)
    n += snprintf(buf + n, sizeof(buf) - n, "<%d>", priority);
    SYSLOG_CLAMP();

    time_t now;
    struct timeval tv;
    if (gettimeofday(&tv, NULL) == 0) now = tv.tv_sec;
    else now = time(NULL);

    struct tm tmv;
    /* No gmtime_r / localtime_r yet in substrate libc; compute the
     * "Mmm DD HH:MM:SS" string by hand from UTC seconds.  syslogd
     * is welcome to re-stamp with localtime later. */
    {
        static const char *mon[] = {
            "Jan","Feb","Mar","Apr","May","Jun",
            "Jul","Aug","Sep","Oct","Nov","Dec"
        };
        /* Days from 1970-01-01 to current date, civil-from-days
         * algorithm (Howard Hinnant).  */
        long secs  = (long)now;
        long days  = secs / 86400;
        long tod   = secs - days * 86400;
        int  hour  = tod / 3600;
        int  min   = (tod % 3600) / 60;
        int  sec   = tod % 60;
        long z     = days + 719468;
        long era   = (z >= 0 ? z : z - 146096) / 146097;
        unsigned doe = z - era * 146097;
        unsigned yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
        long y     = (long)yoe + era * 400;
        unsigned doy  = doe - (365*yoe + yoe/4 - yoe/100);
        unsigned mp   = (5*doy + 2) / 153;
        unsigned d    = doy - (153*mp + 2)/5 + 1;
        unsigned mo   = mp + (mp < 10 ? 3 : -9);
        y += (mo <= 2);
        (void)y;
        tmv.tm_mon = mo - 1;
        tmv.tm_mday = d;
        tmv.tm_hour = hour;
        tmv.tm_min  = min;
        tmv.tm_sec  = sec;
        n += snprintf(buf + n, sizeof(buf) - n,
                      "%s %2d %02d:%02d:%02d ",
                      mon[tmv.tm_mon], tmv.tm_mday,
                      tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    }
    SYSLOG_CLAMP();

    if (g_ident && *g_ident) {
        if (g_option & LOG_PID)
            n += snprintf(buf + n, sizeof(buf) - n, "%s[%d]: ",
                          g_ident, (int)getpid());
        else
            n += snprintf(buf + n, sizeof(buf) - n, "%s: ", g_ident);
        SYSLOG_CLAMP();
    }
    n += vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
    SYSLOG_CLAMP();
#undef SYSLOG_CLAMP
    /* Trailing newline doubles as our stream-mode record delimiter
     * (substrate AF_UNIX is SOCK_STREAM only).  Daemon trims it.  */
    if (n + 1 < (int)sizeof(buf)) buf[n++] = '\n';

    try_open();
    int sent = 0;
    if (g_logfd >= 0) {
        ssize_t r = send(g_logfd, buf, n, 0);
        if (r >= 0) sent = 1;
        else if (errno == EPIPE || errno == ECONNRESET) {
            /* Daemon went away — drop the fd and let next call
             * reconnect on demand. */
            close(g_logfd);
            g_logfd = -1;
        }
    }

    /* Fallback path on no daemon. */
    if (!sent && (g_option & LOG_CONS)) {
        int fd = open(SYSLOG_FALLBACK, O_WRONLY | O_NOCTTY);
        if (fd >= 0) {
            write(fd, buf, n);
            write(fd, "\n", 1);
            close(fd);
        }
    }

    if (g_option & LOG_PERROR) {
        write(2, buf, n);
        write(2, "\n", 1);
    }
}

void syslog(int priority, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsyslog(priority, fmt, ap);
    va_end(ap);
}
