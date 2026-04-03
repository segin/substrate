#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <ctype.h>
#include <strings.h>

#include <at.h>

/*
 * Helper to parse a time string.
 * Handles:
 * - HH:MM[am/pm]
 * - HHMM[am/pm]
 * - Keywords: now, noon, midnight, teatime, tomorrow
 * Returns 0 on success, -1 on failure.
 */
static int parse_base_time(const char *buf, struct tm *info, time_t *t_base, const char **next) {
    time_t now = *t_base;
    int h, m, n;
    int is_numeric = 0;

    if (strncmp(buf, "now", 3) == 0) {
        *next = buf + 3;
        return 0;
    } else if (strncmp(buf, "noon", 4) == 0) {
        info->tm_hour = 12; info->tm_min = 0; info->tm_sec = 0;
        *t_base = mktime(info);
        if (*t_base < now) *t_base += 86400;
        *next = buf + 4;
        return 0;
    } else if (strncmp(buf, "midnight", 8) == 0) {
        info->tm_hour = 0; info->tm_min = 0; info->tm_sec = 0;
        *t_base = mktime(info);
        if (*t_base < now) *t_base += 86400;
        *next = buf + 8;
        return 0;
    } else if (strncmp(buf, "teatime", 7) == 0) {
        info->tm_hour = 16; info->tm_min = 0; info->tm_sec = 0;
        *t_base = mktime(info);
        if (*t_base < now) *t_base += 86400;
        *next = buf + 7;
        return 0;
    } else if (strncmp(buf, "tomorrow", 8) == 0) {
        *t_base = now + 86400;
        localtime_r(t_base, info);
        *next = buf + 8;
        return 0;
    }

    if (sscanf(buf, "%d:%d%n", &h, &m, &n) == 2) {
        if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
        info->tm_hour = h; info->tm_min = m; info->tm_sec = 0;
        is_numeric = 1;
    } else if (sscanf(buf, "%4d%n", &h, &n) == 1) {
        int temp_h, temp_m;
        if (h >= 100) {
            temp_h = h / 100;
            temp_m = h % 100;
        } else {
            temp_h = h;
            temp_m = 0;
        }
        if (temp_h < 0 || temp_h > 23 || temp_m < 0 || temp_m > 59) return -1;
        info->tm_hour = temp_h;
        info->tm_min = temp_m;
        info->tm_sec = 0;
        is_numeric = 1;
    }

    if (is_numeric) {
        const char *p = buf + n;
        int space_len = 0;
        while (isspace((unsigned char)p[space_len])) {
            space_len++;
        }
        if (strncasecmp(p + space_len, "am", 2) == 0) {
            if (info->tm_hour == 12) info->tm_hour = 0;
            n += space_len + 2;
        } else if (strncasecmp(p + space_len, "pm", 2) == 0) {
            if (info->tm_hour < 12) info->tm_hour += 12;
            n += space_len + 2;
        }
        *t_base = mktime(info);
        if (*t_base < now) *t_base += 86400;
        *next = buf + n;
        return 0;
    }

    return -1;
}

/*
 * Helper to parse increments like "+ 5 minutes".
 * Returns 0 on success, -1 on invalid format.
 */
static int parse_increment(const char *buf, time_t *t) {
    while (isspace((unsigned char)*buf)) buf++;
    if (*buf == '\0') return 0;
    if (*buf != '+') return -1;
    buf++;
    while (isspace((unsigned char)*buf)) buf++;

    int count;
    int n;
    if (sscanf(buf, "%d%n", &count, &n) != 1) return -1;
    buf += n;
    while (isspace((unsigned char)*buf)) buf++;

    long unit_sec = 60;
    if (strncmp(buf, "minutes", 7) == 0 && (isspace((unsigned char)buf[7]) || buf[7] == '\0')) {
        unit_sec = 60;
    } else if (strncmp(buf, "minute", 6) == 0 && (isspace((unsigned char)buf[6]) || buf[6] == '\0')) {
        unit_sec = 60;
    } else if (strncmp(buf, "hours", 5) == 0 && (isspace((unsigned char)buf[5]) || buf[5] == '\0')) {
        unit_sec = 3600;
    } else if (strncmp(buf, "hour", 4) == 0 && (isspace((unsigned char)buf[4]) || buf[4] == '\0')) {
        unit_sec = 3600;
    } else if (strncmp(buf, "days", 4) == 0 && (isspace((unsigned char)buf[4]) || buf[4] == '\0')) {
        unit_sec = 86400;
    } else if (strncmp(buf, "day", 3) == 0 && (isspace((unsigned char)buf[3]) || buf[3] == '\0')) {
        unit_sec = 86400;
    } else if (strncmp(buf, "weeks", 5) == 0 && (isspace((unsigned char)buf[5]) || buf[5] == '\0')) {
        unit_sec = 86400 * 7;
    } else if (strncmp(buf, "week", 4) == 0 && (isspace((unsigned char)buf[4]) || buf[4] == '\0')) {
        unit_sec = 86400 * 7;
    } else {
        return -1;
    }

    *t += (time_t)count * unit_sec;
    return 0;
}

/*
 * at_parse_time: parses human readable timespecs.
 * Support for keywords, absolute time (HH:MM), and relative increments.
 */
int at_parse_time(int argc, char *argv[], int optind, time_t *out_time) {
    if (optind >= argc) {
        *out_time = 0; // Immediate/now
        return 0;
    }

    // Concatenate remaining args into a timespec buffer
    char buf[256] = {0};
    for (int i = optind; i < argc; i++) {
        strncat(buf, argv[i], sizeof(buf) - strlen(buf) - 1);
        if (i < argc - 1) {
            strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
        }
    }

    time_t now = time(NULL);
    struct tm info;
    localtime_r(&now, &info);

    time_t t_base = now;
    const char *next = NULL;
    if (parse_base_time(buf, &info, &t_base, &next) != 0) {
        return -1;
    }

    if (parse_increment(next, &t_base) != 0) {
        return -1;
    }

    *out_time = t_base;
    return 0;
}
