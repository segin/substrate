#include <time.h>
#include <sys/types.h>
#include <sys/time.h> // for gettimeofday
#include <sys/times.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>

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
        days += IS_LEAP(year) ? 366 : 365;
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

// TZ Rule structure for DST start/end
struct tz_rule {
    enum { RULE_J, RULE_N, RULE_M } type;
    int m; // Month (1-12) for M rule
    int n; // Week (1-5) for M rule, or Day (0-365 or 1-365) for J/N rule
    int d; // Day of week (0-6) for M rule
    int time_sec; // Time of transition in seconds from midnight (default 02:00:00 = 7200)
};

// Parsed TZ information
struct tz_info {
    char std_name[16];
    long std_off; // Seconds West of UTC
    char dst_name[16];
    long dst_off; // Seconds West of UTC
    struct tz_rule start;
    struct tz_rule end;
    int has_dst;
};

// Helper to parse time offset: [+|-]hh[:mm[:ss]]
static const char *parse_offset(const char *p, long *offset) {
    if (!p) return NULL;
    char *end;
    long h = strtol(p, &end, 10);
    long m = 0, s = 0;
    if (*end == ':') {
        m = strtol(end + 1, &end, 10);
        if (*end == ':') {
            s = strtol(end + 1, &end, 10);
        }
    }

    // Apply sign to minutes/seconds (if h is negative, they subtract from total)
    int sign = (h < 0 || *p == '-') ? -1 : 1;

    *offset = (h * 3600) + (sign * m * 60) + (sign * s);
    return end;
}

// Helper to parse rule: Mm.w.d or Jn or n
static const char *parse_rule(const char *p, struct tz_rule *rule) {
    if (!p) return NULL;
    char *end;

    if (*p == 'M') {
        rule->type = RULE_M;
        rule->m = strtol(p + 1, &end, 10);
        if (*end != '.') return NULL;
        rule->n = strtol(end + 1, &end, 10); // week
        if (*end != '.') return NULL;
        rule->d = strtol(end + 1, &end, 10); // day
    } else if (*p == 'J') {
        rule->type = RULE_J;
        rule->n = strtol(p + 1, &end, 10);
    } else if (isdigit(*p)) {
        rule->type = RULE_N;
        rule->n = strtol(p, &end, 10);
    } else {
        return NULL;
    }

    // Optional time part /time
    rule->time_sec = 7200; // Default 02:00:00
    if (end && *end == '/') {
        long off;
        const char *next = parse_offset(end + 1, &off);
        if (next) {
            rule->time_sec = (int)off;
            end = (char*)next;
        }
    }

    return end;
}

static void parse_tz(const char *tz, struct tz_info *info) {
    if (!tz || !*tz) {
        // Default to UTC
        strcpy(info->std_name, "UTC");
        info->std_off = 0;
        info->has_dst = 0;
        return;
    }

    const char *p = tz;

    // 1. Std Name
    int i = 0;
    if (*p == '<') {
        p++;
        while (*p && *p != '>' && i < 15) info->std_name[i++] = *p++;
        if (*p == '>') p++;
    } else {
        while (*p && !isdigit(*p) && *p != '+' && *p != '-' && *p != ',' && i < 15) info->std_name[i++] = *p++;
    }
    info->std_name[i] = 0;

    // 2. Std Offset
    p = parse_offset(p, &info->std_off);
    if (!p || !*p) {
        info->has_dst = 0;
        return;
    }

    // 3. DST Name
    i = 0;
    if (*p == '<') {
        p++;
        while (*p && *p != '>' && i < 15) info->dst_name[i++] = *p++;
        if (*p == '>') p++;
    } else {
        while (*p && !isdigit(*p) && *p != '+' && *p != '-' && *p != ',' && i < 15) info->dst_name[i++] = *p++;
    }
    info->dst_name[i] = 0;

    if (i == 0) { // No DST name found
        info->has_dst = 0;
        return;
    }

    info->has_dst = 1;

    // 4. DST Offset (Optional)
    if (*p != ',' && *p != ';') {
        const char *next = parse_offset(p, &info->dst_off);
        if (next && next != p) p = next;
        else info->dst_off = info->std_off - 3600; // Default 1 hour less offset (so 1 hour ahead)
    } else {
         info->dst_off = info->std_off - 3600;
    }

    // 5. Start/End Rules (Optional)
    if (*p == ',' || *p == ';') {
        p++;
        p = parse_rule(p, &info->start);
        if (p && (*p == ',' || *p == ';')) {
            p++;
            parse_rule(p, &info->end);
        }
    } else {
        // Default rules: US (M3.2.0, M11.1.0)
        // Start: 2nd Sunday in March
        info->start.type = RULE_M; info->start.m = 3; info->start.n = 2; info->start.d = 0; info->start.time_sec = 7200;
        // End: 1st Sunday in November
        info->end.type = RULE_M; info->end.m = 11; info->end.n = 1; info->end.d = 0; info->end.time_sec = 7200;
    }
}

// Internal timegm (UTC tm -> time_t)
static time_t internal_timegm(struct tm *tm) {
    int year = tm->tm_year + 1900;
    int mon = tm->tm_mon;

    while (mon < 0) { mon += 12; year--; }
    while (mon > 11) { mon -= 12; year++; }

    long days = 0;
    if (year >= 1970) {
        for (int y = 1970; y < year; y++) {
            days += IS_LEAP(y) ? 366 : 365;
        }
    } else {
        for (int y = 1969; y >= year; y--) {
            days -= IS_LEAP(y) ? 366 : 365;
        }
    }

    for (int m = 0; m < mon; m++) {
        int dim = days_in_month[m];
        if (m == 1 && IS_LEAP(year)) dim++;
        days += dim;
    }
    days += (tm->tm_mday - 1);

    time_t t = (time_t)days * SECS_PER_DAY;
    t += tm->tm_hour * SECS_PER_HOUR;
    t += tm->tm_min * SECS_PER_MIN;
    t += tm->tm_sec;
    return t;
}

// Helper: Get Nth Weekday of Month
static int get_nth_weekday(int year, int month, int n, int wday) {
    struct tm tm = {0};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1; // 0-11
    tm.tm_mday = 1;

    time_t t = internal_timegm(&tm);
    gmtime_r(&t, &tm);
    int first_wday = tm.tm_wday;

    int day = 1 + (wday - first_wday + 7) % 7;

    if (n < 5) {
        day += (n - 1) * 7;
    } else {
        while (1) {
            int next = day + 7;
            int dim = days_in_month[month - 1];
            if (month == 2 && IS_LEAP(year)) dim++;
            if (next > dim) break;
            day = next;
        }
    }
    return day;
}

// Calculate transition time in UTC for a given year and rule
static time_t get_transition_time(int year, struct tz_rule *rule, long offset_sec) {
    struct tm tm = {0};
    tm.tm_year = year - 1900;
    tm.tm_sec = rule->time_sec;

    if (rule->type == RULE_M) {
        tm.tm_mon = rule->m - 1;
        tm.tm_mday = get_nth_weekday(year, rule->m, rule->n, rule->d);
    } else if (rule->type == RULE_J) {
        int is_leap = IS_LEAP(year);
        int day_idx = rule->n;
        if (is_leap && day_idx >= 60) day_idx++;
        tm.tm_mon = 0; tm.tm_mday = day_idx;
    } else if (rule->type == RULE_N) {
        tm.tm_mon = 0; tm.tm_mday = rule->n + 1;
    }

    time_t t = internal_timegm(&tm);
    t += offset_sec;
    return t;
}

struct tm *localtime_r(const time_t *__restrict timer, struct tm *__restrict result) {
    if (!timer || !result) return NULL;

    const char *env_tz = getenv("TZ");
    struct tz_info info;
    parse_tz(env_tz, &info);

    time_t t = *timer;
    struct tm tm_utc;
    gmtime_r(&t, &tm_utc);
    int year = tm_utc.tm_year + 1900;

    long offset = info.std_off;
    int is_dst = 0;

    if (info.has_dst) {
        time_t t_start = get_transition_time(year, &info.start, info.std_off);
        time_t t_end = get_transition_time(year, &info.end, info.dst_off);

        bool in_dst = false;
        if (t_start < t_end) {
            if (t >= t_start && t < t_end) in_dst = true;
        } else {
            if (t >= t_start || t < t_end) in_dst = true;
        }

        if (in_dst) {
            offset = info.dst_off;
            is_dst = 1;
        }
    }

    t -= offset;
    gmtime_r(&t, result);

    result->tm_gmtoff = -offset;

    static char zone_names[2][16];
    strcpy(zone_names[0], info.std_name);
    if (info.has_dst) strcpy(zone_names[1], info.dst_name);

    result->tm_zone = is_dst ? zone_names[1] : zone_names[0];
    result->tm_isdst = is_dst;

    return result;
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
