/*
 * who — show who is logged on.
 *
 * Reads the login records in /var/run/utmp and prints one line per
 * logged-in user (USER_PROCESS).  With -b, prints the system boot
 * time instead.  With -H, prints a column header.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <utmp.h>

/*
 * Copy a utmp char field (not guaranteed NUL-terminated), replacing any
 * non-printable byte with '?'.  ut_host is set by telnetd from the network
 * peer, so printing it raw would inject terminal-escape sequences into the
 * operator's terminal (WHO-01).
 */
static void field(char *dst, const char *src, size_t n)
{
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        unsigned char c = (unsigned char)src[i];
        dst[i] = (c >= 0x20 && c < 0x7f) ? (char)c : '?';
    }
    dst[i] = '\0';
}

int main(int argc, char **argv)
{
    int show_boot = 0, want_header = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-b") == 0)      show_boot = 1;
        else if (strcmp(argv[i], "-H") == 0) want_header = 1;
        else {
            fprintf(stderr, "usage: who [-b] [-H]\n");
            return 2;
        }
    }

    if (want_header)
        printf("%-9s %-12s %-17s %s\n", "NAME", "LINE", "TIME", "COMMENT");

    setutent();
    struct utmp *u;
    while ((u = getutent()) != NULL) {
        if (show_boot) { if (u->ut_type != BOOT_TIME)    continue; }
        else           { if (u->ut_type != USER_PROCESS) continue; }

        char line[UT_LINESIZE + 1], user[UT_NAMESIZE + 1], host[UT_HOSTSIZE + 1];
        field(line, u->ut_line, UT_LINESIZE);
        field(user, u->ut_user, UT_NAMESIZE);
        field(host, u->ut_host, UT_HOSTSIZE);

        char tbuf[32] = "";
        time_t t = (time_t)u->ut_tv.tv_sec;
        struct tm *tm = localtime(&t);
        if (tm) strftime(tbuf, sizeof tbuf, "%Y-%m-%d %H:%M", tm);

        if (show_boot) {
            printf("%-9s %-12s %s\n", "reboot", "system boot", tbuf);
        } else if (host[0]) {
            printf("%-9s %-12s %-17s (%s)\n", user, line, tbuf, host);
        } else {
            printf("%-9s %-12s %s\n", user, line, tbuf);
        }
    }
    endutent();
    return 0;
}
