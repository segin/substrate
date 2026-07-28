/*
 * write - send a message to another user's terminal.
 *
 *   write user [ttyname]
 *
 * Locates the target user's login terminal in /var/run/utmp (or uses the
 * ttyname operand when given), then copies stdin to that terminal line by
 * line, each line sanitized against terminal-escape injection.  The old
 * stub read and discarded stdin and never wrote anything, so the message
 * silently went nowhere.
 *
 * Exit status: 0 on success, 1 if the user is not logged in / not
 * writable / on error.
 */

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "utmp.h"

static const char *prog = "write";

/* Write the whole buffer, retrying short writes/EINTR. */
static int
full_write(int fd, const char *buf, size_t n)
{
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, buf + off, n - off);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        off += (size_t)w;
    }
    return 0;
}

/* Sanitize one line for a terminal: printable + tab pass through; other
 * control bytes become ^X, high-bit bytes M-.  Prevents a sender from
 * injecting escape sequences (retitle/clear/OSC52) into the recipient's
 * terminal. */
static void
put_sanitized(int fd, const char *s, size_t n)
{
    char out[8];
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        size_t o = 0;
        if (c == '\n' || c == '\t') { out[o++] = (char)c; }
        else if (c == '\r') { continue; }
        else {
            if (c >= 0x80) { out[o++] = 'M'; out[o++] = '-'; c = (unsigned char)(c & 0x7f); }
            if (c < 0x20 || c == 0x7f) { out[o++] = '^'; out[o++] = (char)(c ^ 0x40); }
            else                       { out[o++] = (char)c; }
        }
        (void)full_write(fd, out, o);
    }
}

/* Find the target user's terminal device.  On success writes "/dev/<line>"
 * into dev and returns 0. */
static int
find_user_tty(const char *user, const char *want_line, char *dev, size_t devsz)
{
    FILE *ut = fopen(UTMP_FILE, "r");
    if (!ut) {
        fprintf(stderr, "%s: cannot open %s\n", prog, UTMP_FILE);
        return -1;
    }

    struct utmp u;
    int found = -1;
    while (fread(&u, sizeof u, 1, ut) == 1) {
        if (u.ut_type != USER_PROCESS) continue;
        if (strncmp(u.ut_user, user, UT_NAMESIZE) != 0) continue;

        char line[UT_LINESIZE + 1];
        memcpy(line, u.ut_line, UT_LINESIZE);
        line[UT_LINESIZE] = '\0';

        /* Reject a crafted ut_line escaping /dev. */
        if (line[0] == '\0' || line[0] == '.' || strchr(line, '/'))
            continue;
        if (want_line && strcmp(line, want_line) != 0)
            continue;

        snprintf(dev, devsz, "/dev/%s", line);
        found = 0;
        break;
    }
    fclose(ut);
    if (found != 0)
        fprintf(stderr, "%s: %s is not logged in%s\n", prog, user,
            want_line ? " on that terminal" : "");
    return found;
}

int
main(int argc, char *argv[])
{
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: write user [ttyname]\n");
        return 1;
    }

    const char *user = argv[1];
    const char *want_line = (argc == 3) ? argv[2] : NULL;

    /* If a ttyname was given, still validate it can't escape /dev. */
    if (want_line && (want_line[0] == '.' || strchr(want_line, '/'))) {
        fprintf(stderr, "%s: invalid terminal name\n", prog);
        return 1;
    }

    char dev[8 + UT_LINESIZE];
    if (find_user_tty(user, want_line, dev, sizeof dev) != 0)
        return 1;

    /* O_NOFOLLOW + isatty() gate: a symlink planted at /dev/<line> must
     * not redirect the message into an arbitrary file. */
    int fd = open(dev, O_WRONLY | O_NONBLOCK | O_NOFOLLOW);
    if (fd < 0) {
        fprintf(stderr, "%s: cannot open %s: %s\n", prog, dev, strerror(errno));
        return 1;
    }
    if (!isatty(fd)) {
        fprintf(stderr, "%s: %s is not a terminal\n", prog, dev);
        close(fd);
        return 1;
    }

    /* Announce the sender. */
    const char *me = getlogin();
    if (!me || !*me) {
        struct passwd *pw = getpwuid(getuid());
        me = (pw && pw->pw_name) ? pw->pw_name : "root";
    }
    char myline[UT_LINESIZE + 1] = "?";
    char *tty = ttyname(STDIN_FILENO);
    if (tty) {
        const char *b = strrchr(tty, '/');
        snprintf(myline, sizeof myline, "%s", b ? b + 1 : tty);
    }

    char banner[128];
    int blen = snprintf(banner, sizeof banner,
        "\r\n\007Message from %s on %s ...\r\n", me, myline);
    if (blen > 0)
        (void)full_write(fd, banner, (size_t)blen);

    /* Relay stdin, sanitized, line by line. */
    char buf[1024];
    while (fgets(buf, sizeof buf, stdin))
        put_sanitized(fd, buf, strlen(buf));

    (void)full_write(fd, "EOF\r\n", 5);
    close(fd);
    return 0;
}
