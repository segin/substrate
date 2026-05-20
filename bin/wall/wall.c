/*
 * wall — write a message to every logged-in user's terminal.
 *
 * Reads the message from a file argument, or from stdin when none is
 * given, then writes it — behind the customary banner — to the tty
 * of every USER_PROCESS record in /var/run/utmp.
 *
 *   wall [file]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <pwd.h>
#include <utmp.h>
#include <sys/types.h>

/* Collect the message into `buf` (NUL-terminated); return its length. */
static size_t read_message(const char *path, char *buf, size_t cap)
{
    FILE *src = stdin;
    if (path) {
        src = fopen(path, "r");
        if (!src) { perror(path); exit(1); }
    }
    size_t n = 0;
    int c;
    while (n < cap - 1 && (c = fgetc(src)) != EOF)
        buf[n++] = (char)c;
    buf[n] = '\0';
    if (src != stdin) fclose(src);
    return n;
}

/* Best-effort name of the invoking user. */
static const char *who_am_i(void)
{
    const char *u = getlogin();
    if (u && u[0]) return u;
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_name) return pw->pw_name;
    return "root";
}

int main(int argc, char *argv[])
{
    static char msg[8192];
    size_t mlen = read_message(argc > 1 ? argv[1] : NULL, msg, sizeof msg);

    /* Compose the banner. */
    char host[64] = "localhost";
    gethostname(host, sizeof host);
    host[sizeof host - 1] = '\0';

    char when[16] = "";
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (tm) strftime(when, sizeof when, "%H:%M", tm);

    char banner[256];
    int blen = snprintf(banner, sizeof banner,
        "\r\n\007Broadcast message from %s@%s at %s ...\r\n\r\n",
        who_am_i(), host, when);
    if (blen < 0) blen = 0;

    /* Fan the message out to every logged-in user's terminal. */
    FILE *ut = fopen(UTMP_FILE, "r");
    if (!ut) {
        fprintf(stderr, "wall: cannot open %s\n", UTMP_FILE);
        return 1;
    }

    struct utmp u;
    int delivered = 0;
    while (fread(&u, sizeof u, 1, ut) == 1) {
        if (u.ut_type != USER_PROCESS) continue;
        if (u.ut_line[0] == '\0') continue;

        char dev[8 + UT_LINESIZE];
        char line[UT_LINESIZE + 1];
        memcpy(line, u.ut_line, UT_LINESIZE);
        line[UT_LINESIZE] = '\0';
        snprintf(dev, sizeof dev, "/dev/%s", line);

        /* O_NONBLOCK: never block wall on a wedged terminal. */
        int fd = open(dev, O_WRONLY | O_NONBLOCK);
        if (fd < 0) continue;
        (void)write(fd, banner, (size_t)blen);
        (void)write(fd, msg, mlen);
        (void)write(fd, "\r\n", 2);
        close(fd);
        delivered++;
    }
    fclose(ut);

    if (delivered == 0)
        fprintf(stderr, "wall: no logged-in users\n");
    return 0;
}
