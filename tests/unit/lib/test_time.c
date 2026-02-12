#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

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

bool test_gmtime_negative_years(void) {
    struct tm tm_res;
    time_t t;
    bool passed = true;

    // Test 1: Epoch (1970-01-01 00:00:00)
    t = 0;
    if (our_gmtime_r(&t, &tm_res) == NULL) {
        printf("FAILED: Epoch returned NULL\n");
        return false;
    }
    if (tm_res.tm_year != 70) {
        printf("FAILED: Epoch year %d != 70\n", tm_res.tm_year);
        passed = false;
    }
    if (tm_res.tm_mon != 0 || tm_res.tm_mday != 1) {
        printf("FAILED: Epoch date wrong %d-%d\n", tm_res.tm_mon, tm_res.tm_mday);
        passed = false;
    }

    // Test 2: 1969-12-31 23:59:59 (t = -1)
    t = -1;
    if (our_gmtime_r(&t, &tm_res) == NULL) {
        printf("FAILED: -1 returned NULL\n");
        return false;
    }
    if (tm_res.tm_year != 69) {
        printf("FAILED: -1 year %d != 69\n", tm_res.tm_year);
        passed = false;
    }
    if (tm_res.tm_mon != 11) {
        printf("FAILED: -1 mon %d != 11\n", tm_res.tm_mon);
        passed = false;
    }
    if (tm_res.tm_mday != 31) {
        printf("FAILED: -1 mday %d != 31\n", tm_res.tm_mday);
        passed = false;
    }

    // Test 3: 1900-01-01 (Non-leap year century)
    t = -2208988800L;
    if (our_gmtime_r(&t, &tm_res) == NULL) {
        printf("FAILED: 1900 returned NULL\n");
        return false;
    }
    if (tm_res.tm_year != 0) {
        printf("FAILED: 1900 year %d != 0\n", tm_res.tm_year);
        passed = false;
    }

    // Test 4: Leap year before 1970 (1968-02-29)
    t = -58060800L;
    if (our_gmtime_r(&t, &tm_res) == NULL) {
        printf("FAILED: 1968 returned NULL\n");
        return false;
    }
    if (tm_res.tm_year != 68) {
        printf("FAILED: 1968 year %d != 68\n", tm_res.tm_year);
        passed = false;
    }
    if (tm_res.tm_mon != 1 || tm_res.tm_mday != 29) {
        printf("FAILED: 1968 date wrong %d-%d\n", tm_res.tm_mon, tm_res.tm_mday);
        passed = false;
    }

    return passed;
}

bool test_libc_time(void) {
    time_t t = 1672531200; // 2023-01-01 00:00:00 UTC (Sunday)
    struct tm tm_buf;
    bool all_passed = true;

    // 1. Test UTC (Default/Empty TZ)
    mock_tz = NULL;
    our_localtime_r(&t, &tm_buf);
    if (tm_buf.tm_hour != 0) { printf("Fail UTC hour: %d\n", tm_buf.tm_hour); all_passed = false; }
    if (strcmp(tm_buf.tm_zone, "UTC") != 0) { printf("Fail UTC zone: %s\n", tm_buf.tm_zone); all_passed = false; }

    // 2. Test Fixed Offset (EST5 = UTC-5)
    mock_tz = "EST5";
    our_localtime_r(&t, &tm_buf);
    if (tm_buf.tm_hour != 19) { printf("Fail EST5 hour: %d (expected 19)\n", tm_buf.tm_hour); all_passed = false; }

    // 3. Test Negative Offset (CET-1 = UTC+1)
    mock_tz = "CET-1";
    our_localtime_r(&t, &tm_buf);
    if (tm_buf.tm_hour != 1) { printf("Fail CET-1 hour: %d (expected 1)\n", tm_buf.tm_hour); all_passed = false; }

    // 4. Test DST (EST5EDT) - Standard Time
    mock_tz = "EST5EDT";
    our_localtime_r(&t, &tm_buf);
    if (tm_buf.tm_isdst != 0) { printf("Fail EST5EDT Winter isdst: %d\n", tm_buf.tm_isdst); all_passed = false; }

    // 5. Test DST (EST5EDT) - Summer Time
    time_t t_summer = 1688169600; // 2023-07-01 00:00:00 UTC
    our_localtime_r(&t_summer, &tm_buf);
    if (tm_buf.tm_isdst != 1) { printf("Fail EST5EDT Summer isdst: %d\n", tm_buf.tm_isdst); all_passed = false; }

    return all_passed;
}

#ifdef STANDALONE
int main() {
    bool p1 = test_gmtime_negative_years();
    bool p2 = test_libc_time();
    if (p1 && p2) {
        printf("PASS\n");
        return 0;
    } else {
        printf("FAIL\n");
        return 1;
    }
}
#endif
