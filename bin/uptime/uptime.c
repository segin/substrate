/*
 * uptime — show how long the system has been up.
 *
 * Format:
 *   12:34:56 up 1 day,  2:03,  1 user,  load average: 0.00, 0.00, 0.00
 *
 * Reads /proc/uptime (seconds, idle) and /proc/loadavg.  User count
 * comes from /var/run/utmp if it's parseable; absent → "1 user".
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "utmp.h"

static double read_uptime_seconds(void) {
    int fd = open("/proc/uptime", O_RDONLY);
    if (fd < 0) return 0;
    char buf[128];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';
    return strtod(buf, NULL);
}

static void read_loadavg(double load[3]) {
    load[0] = load[1] = load[2] = 0.0;
    int fd = open("/proc/loadavg", O_RDONLY);
    if (fd < 0) return;
    char buf[128];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';
    char *end;
    load[0] = strtod(buf, &end);
    load[1] = strtod(end, &end);
    load[2] = strtod(end, &end);
}

static int count_users(void) {
    /* Count USER_PROCESS records in utmp (UPTIME-01: was a hardcoded 1). */
    FILE *f = fopen(UTMP_FILE, "r");
    struct utmp u;
    int n = 0;
    if (f == NULL)
        return 0;
    while (fread(&u, sizeof u, 1, f) == 1) {
        if (u.ut_type == USER_PROCESS && u.ut_user[0] != '\0')
            n++;
    }
    fclose(f);
    return n;
}

int main(void) {
    double up = read_uptime_seconds();
    double load[3];
    read_loadavg(load);

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char clock[16];
    if (tm) {
        snprintf(clock, sizeof(clock), "%02d:%02d:%02d",
                 tm->tm_hour, tm->tm_min, tm->tm_sec);
    } else {
        snprintf(clock, sizeof(clock), "??:??:??");
    }

    long total = (long)up;
    long days  = total / 86400;
    long hrs   = (total % 86400) / 3600;
    long mins  = (total % 3600) / 60;

    char up_str[64];
    if (days > 0) {
        snprintf(up_str, sizeof(up_str), "%ld day%s, %2ld:%02ld",
                 days, days == 1 ? "" : "s", hrs, mins);
    } else if (hrs > 0) {
        snprintf(up_str, sizeof(up_str), "%2ld:%02ld", hrs, mins);
    } else {
        snprintf(up_str, sizeof(up_str), "%ld min", mins);
    }

    int users = count_users();
    printf(" %s up %s,  %d user%s,  load average: %.2f, %.2f, %.2f\n",
           clock, up_str, users, users == 1 ? "" : "s",
           load[0], load[1], load[2]);
    return 0;
}
