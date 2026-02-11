#include <time.h>
#include <sys/types.h>
#include <sys/time.h> // for gettimeofday
#include <sys/times.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#define SECS_PER_MIN  60
#define SECS_PER_HOUR 3600
#define SECS_PER_DAY  86400

#define DAYS_PER_YEAR 365
#define DAYS_PER_LEAP_YEAR 366

#define IS_LEAP(y) (((y) % 4 == 0 && (y) % 100 != 0) || ((y) % 400 == 0))

static const int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

struct tm *gmtime_r(const time_t *__restrict timer, struct tm *__restrict result) {
    if (!timer || !result) return NULL;

    time_t t = *timer;
    long days, rem;
    int year = 1970;

    days = t / SECS_PER_DAY;
    rem = t % SECS_PER_DAY;

    while (rem < 0) {
        rem += SECS_PER_DAY;
        days--;
    }

    result->tm_hour = rem / SECS_PER_HOUR;
    rem %= SECS_PER_HOUR;
    result->tm_min = rem / SECS_PER_MIN;
    result->tm_sec = rem % SECS_PER_MIN;

    result->tm_wday = (days + 4) % 7; // 1/1/1970 was Thursday (4)
    if (result->tm_wday < 0) result->tm_wday += 7;

    while (days < 0) {
        year--;
        int days_this_year = IS_LEAP(year) ? 366 : 365;
        days += days_this_year;
    }

    while (1) {
        int days_this_year = IS_LEAP(year) ? 366 : 365;
        if (days < days_this_year) break;
        days -= days_this_year;
        year++;
    }

    result->tm_year = year - 1900;
    result->tm_yday = days;
    result->tm_isdst = 0;
    result->tm_gmtoff = 0;
    result->tm_zone = "UTC";

    for (int i = 0; i < 12; i++) {
        int dim = days_in_month[i];
        if (i == 1 && IS_LEAP(year)) dim++;

        if (days < dim) {
            result->tm_mon = i;
            result->tm_mday = days + 1;
            break;
        }
        days -= dim;
    }

    return result;
}

struct tm *gmtime(const time_t *timer) {
    static struct tm tm_buf;
    return gmtime_r(timer, &tm_buf);
}

// Stub for localtime (just calls gmtime for now, assuming UTC)
struct tm *localtime_r(const time_t *__restrict timer, struct tm *__restrict result) {
    // TODO: Timezone support using TZ env var
    return gmtime_r(timer, result);
}

struct tm *localtime(const time_t *timer) {
    static struct tm tm_buf;
    return localtime_r(timer, &tm_buf);
}

time_t mktime(struct tm *timeptr) {
    // Crude implementation, doesn't handle normalization or DST correctly yet
    
    // 1. Normalize
    // (Simplification: Assuming pre-normalized)
    
    int year = timeptr->tm_year + 1900;
    int mon = timeptr->tm_mon;
    
    // Handle month overflow
    while (mon < 0) { mon += 12; year--; }
    while (mon > 11) { mon -= 12; year++; }
    
    long days = 0;
    
    // Add days for years since 1970
    for (int y = 1970; y < year; y++) {
        days += IS_LEAP(y) ? 366 : 365;
    }
    
    // Add days for months in current year
    for (int m = 0; m < mon; m++) {
        int dim = days_in_month[m];
        if (m == 1 && IS_LEAP(year)) dim++;
        days += dim;
    }
    
    days += (timeptr->tm_mday - 1);
    
    time_t t = days * SECS_PER_DAY;
    t += timeptr->tm_hour * SECS_PER_HOUR;
    t += timeptr->tm_min * SECS_PER_MIN;
    t += timeptr->tm_sec;
    
    return t;
}

char *asctime_r(const struct tm *__restrict timeptr, char *__restrict buf) {
    static const char wday_name[][4] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static const char mon_name[][4] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    sprintf(buf, "%.3s %.3s%3d %.2d:%.2d:%.2d %d\n",
        wday_name[timeptr->tm_wday],
        mon_name[timeptr->tm_mon],
        timeptr->tm_mday,
        timeptr->tm_hour,
        timeptr->tm_min,
        timeptr->tm_sec,
        1900 + timeptr->tm_year);
    return buf;
}

char *asctime(const struct tm *timeptr) {
    static char buf[26];
    return asctime_r(timeptr, buf);
}

char *ctime_r(const time_t *timer, char *buf) {
    struct tm tm;
    if (!localtime_r(timer, &tm)) return NULL;
    return asctime_r(&tm, buf);
}

char *ctime(const time_t *timer) {
    static char buf[26];
    return ctime_r(timer, buf);
}

size_t strftime(char *__restrict s, size_t maxsize, const char *__restrict format, const struct tm *__restrict tp) {
    // Minimal implementation
    size_t count = 0;
    for (const char *p = format; *p; p++) {
        if (*p != '%') {
            if (count < maxsize - 1) s[count++] = *p;
            continue;
        }
        
        p++;
        char tmp[64];
        switch (*p) {
            case 'Y': sprintf(tmp, "%d", tp->tm_year + 1900); break;
            case 'm': sprintf(tmp, "%.2d", tp->tm_mon + 1); break;
            case 'd': sprintf(tmp, "%.2d", tp->tm_mday); break;
            case 'H': sprintf(tmp, "%.2d", tp->tm_hour); break;
            case 'M': sprintf(tmp, "%.2d", tp->tm_min); break;
            case 'S': sprintf(tmp, "%.2d", tp->tm_sec); break;
            case '%': strcpy(tmp, "%"); break;
            default: tmp[0] = 0; break;
        }
        
        size_t len = strlen(tmp);
        if (count + len < maxsize - 1) {
            strcpy(s + count, tmp);
            count += len;
        }
    }
    s[count] = 0;
    return count;
}

clock_t clock(void) {
    struct tms buf;
    if (times(&buf) == (clock_t)-1) {
        return (clock_t)-1;
    }

    const unsigned long tickrate = 100;

    unsigned long long total_ticks = (unsigned long long)buf.tms_utime + buf.tms_stime;

    return (clock_t)((total_ticks * CLOCKS_PER_SEC) / tickrate);
}

double difftime(time_t time1, time_t time0) {
    return (double)(time1 - time0);
}
