#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

#include <at.h>

/* Phase 6.2: BSD extended time parsing */
int at_parse_time(int argc, char *argv[], int optind, time_t *out_time) {
    if (optind >= argc) {
        *out_time = 0; // Immediate/now
        return 0;
    }

    // Naive concatenation of remaining args into a timespec buffer
    char buf[256] = {0};
    for (int i = optind; i < argc; i++) {
        strncat(buf, argv[i], sizeof(buf) - strlen(buf) - 1);
        if (i < argc - 1) {
            strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
        }
    }

    // Start with current time
    time_t now = time(NULL);
    struct tm *info = localtime(&now);

    if (strcmp(buf, "now") == 0) {
        *out_time = now;
        return 0;
    } else if (strcmp(buf, "teatime") == 0) {
        // BSD extension: 4:00 PM
        info->tm_hour = 16;
        info->tm_min = 0;
        info->tm_sec = 0;
        *out_time = mktime(info);
        if (*out_time < now) {
            info->tm_mday++;
            *out_time = mktime(info);
        }
        return 0;
    } else if (strcmp(buf, "noon") == 0) {
        info->tm_hour = 12;
        info->tm_min = 0;
        info->tm_sec = 0;
        *out_time = mktime(info);
        if (*out_time < now) {
            info->tm_mday++;
            *out_time = mktime(info);
        }
        return 0;
    } else if (strcmp(buf, "midnight") == 0) {
        info->tm_hour = 0;
        info->tm_min = 0;
        info->tm_sec = 0;
        *out_time = mktime(info);
        if (*out_time < now) {
            info->tm_mday++;
            *out_time = mktime(info);
        }
        return 0;
    } else if (strcmp(buf, "tomorrow") == 0) {
        info->tm_mday++;
        *out_time = mktime(info);
        return 0;
    }

    /* Additional parsing for "Jan 5", "now + 2 hours", "0400", etc. */
    struct tm tm_parsed;
    char *ret;

    /* Check for "now + X units" */
    if (strncmp(buf, "now + ", 6) == 0) {
        int amount = 0;
        char unit[32] = {0};
        if (sscanf(buf + 6, "%d %31s", &amount, unit) == 2) {
            if (strncmp(unit, "minute", 6) == 0) {
                *out_time = now + (amount * 60);
                return 0;
            } else if (strncmp(unit, "hour", 4) == 0) {
                *out_time = now + (amount * 3600);
                return 0;
            } else if (strncmp(unit, "day", 3) == 0) {
                *out_time = now + (amount * 86400);
                return 0;
            } else if (strncmp(unit, "week", 4) == 0) {
                *out_time = now + (amount * 604800);
                return 0;
            }
        }
    }

    /* Try "HHMM" or "HH:MM" */
    memset(&tm_parsed, 0, sizeof(tm_parsed));
    tm_parsed.tm_year = info->tm_year;
    tm_parsed.tm_mon = info->tm_mon;
    tm_parsed.tm_mday = info->tm_mday;
    tm_parsed.tm_isdst = -1;

    ret = strptime(buf, "%H:%M", &tm_parsed);
    if (!ret) ret = strptime(buf, "%H%M", &tm_parsed);
    if (ret && *ret == '\0') {
        *out_time = mktime(&tm_parsed);
        if (*out_time < now) {
            tm_parsed.tm_mday++;
            *out_time = mktime(&tm_parsed);
        }
        return 0;
    }

    /* Try "MMM DD" (e.g., "Jan 5") */
    memset(&tm_parsed, 0, sizeof(tm_parsed));
    tm_parsed.tm_year = info->tm_year;
    tm_parsed.tm_isdst = -1;
    ret = strptime(buf, "%b %d", &tm_parsed);
    if (ret && *ret == '\0') {
        /* If the date has already passed this year, assume next year */
        tm_parsed.tm_hour = info->tm_hour;
        tm_parsed.tm_min = info->tm_min;
        tm_parsed.tm_sec = info->tm_sec;
        *out_time = mktime(&tm_parsed);
        if (*out_time < now) {
            tm_parsed.tm_year++;
            *out_time = mktime(&tm_parsed);
        }
        return 0;
    }

    // Since full parsing is complex, fail if unknown to satisfy "deterministic failure" rule
    return -1;
}
