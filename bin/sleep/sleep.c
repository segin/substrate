#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

/*
 * Parse "N", "N.F" or ".F" (decimal digits only) into a timespec.  Digits
 * past nanosecond precision are ignored.  Returns 0, or -1 for a sign,
 * garbage, an empty operand or a value too large for time_t.
 */
static int parse_interval(const char *s, struct timespec *ts) {
    const char *p = s;
    long long sec = 0;
    long nsec = 0;
    int digits = 0;

    while (*p >= '0' && *p <= '9') {
        if (sec > (0x7fffffffLL - 9) / 10) return -1;
        sec = sec * 10 + (*p++ - '0');
        digits++;
    }
    if (*p == '.') {
        long scale = 100000000;
        p++;
        while (*p >= '0' && *p <= '9') {
            nsec += (*p++ - '0') * scale;
            scale /= 10;
            digits++;
        }
    }
    if (digits == 0 || *p != '\0') return -1;
    ts->tv_sec = (time_t)sec;
    ts->tv_nsec = nsec;
    return 0;
}

int main(int argc, char *argv[]) {
    struct timespec ts;

    if (argc < 2) {
        printf("usage: sleep seconds\n");
        return 1;
    }
    if (parse_interval(argv[1], &ts) < 0) {
        fprintf(stderr, "sleep: invalid time interval '%s'\n", argv[1]);
        return 1;
    }
    /* A signal that is caught and returns interrupts nanosleep(); carry on
     * with what is left of the interval. */
    while (nanosleep(&ts, &ts) < 0) {
        if (errno != EINTR) {
            perror("sleep");
            return 1;
        }
    }
    return 0;
}
