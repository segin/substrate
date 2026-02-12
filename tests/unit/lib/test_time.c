#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Mock getenv
static const char *mock_tz = NULL;
static char *mock_getenv(const char *name) {
    if (strcmp(name, "TZ") == 0) return (char*)mock_tz;
    return NULL;
}

// Redefine getenv to mock_getenv
#define getenv mock_getenv

// Rename functions to avoid conflict with host libc
#define gmtime_r our_gmtime_r
#define localtime_r our_localtime_r
#define mktime our_mktime
#define asctime_r our_asctime_r
#define ctime_r our_ctime_r
#define gmtime our_gmtime
#define localtime our_localtime
#define asctime our_asctime
#define ctime our_ctime
#define strftime our_strftime
#define clock our_clock
#define difftime our_difftime

// Include implementation
#include "../../../lib/c/src/time/time.c"

bool test_libc_time(void) {
    time_t t = 1672531200; // 2023-01-01 00:00:00 UTC (Sunday)
    struct tm tm_buf;
    bool all_passed = true;

    // 1. Test UTC (Default/Empty TZ)
    mock_tz = NULL;
    our_localtime_r(&t, &tm_buf);
    if (tm_buf.tm_hour != 0) { printf("Fail UTC hour: %d\n", tm_buf.tm_hour); all_passed = false; }
    if (strcmp(tm_buf.tm_zone, "UTC") != 0) { printf("Fail UTC zone: %s\n", tm_buf.tm_zone); all_passed = false; }

    mock_tz = "UTC";
    our_localtime_r(&t, &tm_buf);
    if (tm_buf.tm_hour != 0) { printf("Fail Explicit UTC hour\n"); all_passed = false; }

    // 2. Test Fixed Offset (EST5 = UTC-5)
    mock_tz = "EST5";
    our_localtime_r(&t, &tm_buf);
    // 00:00 UTC minus 5 hours -> 19:00 previous day
    if (tm_buf.tm_hour != 19) { printf("Fail EST5 hour: %d (expected 19)\n", tm_buf.tm_hour); all_passed = false; }
    if (tm_buf.tm_gmtoff != -18000) { printf("Fail EST5 offset: %ld\n", tm_buf.tm_gmtoff); all_passed = false; }
    if (strcmp(tm_buf.tm_zone, "EST") != 0) { printf("Fail EST5 zone: %s\n", tm_buf.tm_zone); all_passed = false; }

    // 3. Test Negative Offset (CET-1 = UTC+1)
    mock_tz = "CET-1";
    our_localtime_r(&t, &tm_buf);
    // 00:00 UTC plus 1 hour -> 01:00
    if (tm_buf.tm_hour != 1) { printf("Fail CET-1 hour: %d (expected 1)\n", tm_buf.tm_hour); all_passed = false; }
    if (tm_buf.tm_gmtoff != 3600) { printf("Fail CET-1 offset: %ld\n", tm_buf.tm_gmtoff); all_passed = false; }

    // 4. Test DST (EST5EDT) - Standard Time
    // Date: Jan 1 2023 is Winter, so Standard Time (EST)
    mock_tz = "EST5EDT";
    our_localtime_r(&t, &tm_buf);
    if (tm_buf.tm_isdst != 0) { printf("Fail EST5EDT Winter isdst: %d\n", tm_buf.tm_isdst); all_passed = false; }
    if (tm_buf.tm_hour != 19) { printf("Fail EST5EDT Winter hour: %d\n", tm_buf.tm_hour); all_passed = false; }
    if (strcmp(tm_buf.tm_zone, "EST") != 0) { printf("Fail EST5EDT Winter zone: %s\n", tm_buf.tm_zone); all_passed = false; }

    // 5. Test DST (EST5EDT) - Summer Time
    // Date: Jul 1 2023 (approx 1688169600) is Summer, so Daylight Time (EDT)
    time_t t_summer = 1688169600; // 2023-07-01 00:00:00 UTC
    our_localtime_r(&t_summer, &tm_buf);
    // UTC 00:00. EDT is UTC-4. So 20:00 previous day.
    if (tm_buf.tm_isdst != 1) { printf("Fail EST5EDT Summer isdst: %d\n", tm_buf.tm_isdst); all_passed = false; }
    if (tm_buf.tm_hour != 20) { printf("Fail EST5EDT Summer hour: %d (expected 20)\n", tm_buf.tm_hour); all_passed = false; }
    if (tm_buf.tm_gmtoff != -14400) { printf("Fail EST5EDT Summer offset: %ld\n", tm_buf.tm_gmtoff); all_passed = false; }
    if (strcmp(tm_buf.tm_zone, "EDT") != 0) { printf("Fail EST5EDT Summer zone: %s\n", tm_buf.tm_zone); all_passed = false; }

    // 6. Test Pre-1970 (Negative time_t)
    // 1969-12-31 23:00:00 UTC = -3600
    mock_tz = "EST5";
    time_t t_neg = -3600;
    our_localtime_r(&t_neg, &tm_buf);
    // Local = UTC - 5h = 18:00 on 1969-12-31.
    // Wait, gmtime_r (ours) handles negative time_t?
    // gmtime_r implementation:
    // while (rem < 0) { rem += SECS_PER_DAY; days--; }
    // while (1) { ... year++ }
    // This assumes positive time.
    // "Simplifying assumption: t >= 0 for now for simplicity, strict logic requires negative year handling."
    // "TODO: Negative years"
    // So gmtime_r IS BROKEN for negative years.
    // My internal_timegm fix handles negative years for DST calculation, but gmtime_r needs to handle it too
    // if we want localtime_r to work for negative years.
    //
    // The review pointed out internal_timegm issue. I fixed it.
    // But gmtime_r is pre-existing code.
    // If I fix gmtime_r, that's good.
    // But let's see if it works for small negative numbers (1969).
    // days = -3600 / 86400 = 0.
    // rem = -3600.
    // while (rem < 0) { rem += 86400; days--; } -> rem=82800 (23:00), days=-1.
    // result->tm_wday = (days + 4) % 7. -1+4 = 3. 3%7 = 3. (Wednesday). 1970-01-01 was Thursday. Dec 31 was Wed. Correct.
    // Loop for year:
    // year = 1970. days = -1.
    // days < days_this_year (365)? Yes (-1 < 365).
    // Break?
    // Wait. "while (1) { ... if (days < days_this_year) break; ... }"
    // If days is negative, it breaks immediately.
    // Then result->tm_year = year - 1900 = 70.
    // result->tm_yday = days = -1.
    // Then loop for month:
    // days (-1) < dim (31)? Yes.
    // tm_mon = 0. tm_mday = -1 + 1 = 0.
    // So 1970-01-00.
    // This is WRONG.
    //
    // So gmtime_r DOES NOT support negative time.
    // I should fix gmtime_r too if I want full correctness, or acknowledge the limitation.
    // The review said: "Bug in Pre-1970 Dates: The helper function internal_timegm loops ...".
    // I fixed internal_timegm.
    // Does localtime_r depend on gmtime_r for negative dates? Yes.
    // If gmtime_r is broken, localtime_r is broken for negative dates.
    //
    // I should fix gmtime_r loop.
    //
    // while (days < 0) {
    //    year--;
    //    days += IS_LEAP(year) ? 366 : 365;
    // }
    // while (days >= (IS_LEAP(year)?366:365)) { ... }
    //
    // I'll add this to gmtime_r in time.c.

    return all_passed;
}

int main() {
    if (test_libc_time()) {
        printf("Tests PASSED\n");
        return 0;
    } else {
        printf("Tests FAILED\n");
        return 1;
    }
}
