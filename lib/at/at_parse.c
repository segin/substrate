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
        if (*out_time < now) *out_time += 86400; // Tomorrow if already past 4 PM
        return 0;
    } else if (strcmp(buf, "noon") == 0) {
        info->tm_hour = 12;
        info->tm_min = 0;
        info->tm_sec = 0;
        *out_time = mktime(info);
        if (*out_time < now) *out_time += 86400; // Tomorrow
        return 0;
    } else if (strcmp(buf, "midnight") == 0) {
        info->tm_hour = 0;
        info->tm_min = 0;
        info->tm_sec = 0;
        *out_time = mktime(info);
        if (*out_time < now) *out_time += 86400; // Tomorrow
        return 0;
    } else if (strcmp(buf, "tomorrow") == 0) {
        *out_time = now + 86400;
        return 0;
    }

    // TODO: A complete lexer/parser (yacc/lex based) for general timespecs
    // like "Jan 5", "now + 2 hours", "0400"
    // Since full parsing is complex, fail if unknown to satisfy "deterministic failure" rule
    return -1;
}
