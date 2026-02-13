#include <stdio.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

// Rename implemented functions to match the prefixed symbols from objcopy
#define gmtime_r substrate_gmtime_r
#define gmtime substrate_gmtime
#define localtime_r substrate_localtime_r
#define localtime substrate_localtime
#define mktime substrate_mktime
#define asctime_r substrate_asctime_r
#define asctime substrate_asctime
#define ctime_r substrate_ctime_r
#define ctime substrate_ctime
#define strftime substrate_strftime
#define difftime substrate_difftime
#define clock substrate_clock
#define timegm substrate_timegm

// Include the source file directly
#include "../../../lib/c/src/time/time.c"

// Forward declarations for renamed functions
struct tm *substrate_gmtime_r(const time_t *__restrict timer, struct tm *__restrict result);

bool test_gmtime_negative_years(void) {
    struct tm tm_res;
    time_t t;
    bool passed = true;

    printf("Running substrate_gmtime_r negative years tests...\n");

    // Test 1: Epoch (1970-01-01 00:00:00)
    t = 0;
    if (substrate_gmtime_r(&t, &tm_res) == NULL) {
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
    if (substrate_gmtime_r(&t, &tm_res) == NULL) {
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
    // t = -2208988800
    t = -2208988800L;
    if (substrate_gmtime_r(&t, &tm_res) == NULL) {
        printf("FAILED: 1900 returned NULL\n");
        return false;
    }
    if (tm_res.tm_year != 0) {
        printf("FAILED: 1900 year %d != 0\n", tm_res.tm_year);
        passed = false;
    }
    if (tm_res.tm_mon != 0) {
        printf("FAILED: 1900 mon %d != 0\n", tm_res.tm_mon);
        passed = false;
    }
    if (tm_res.tm_mday != 1) {
        printf("FAILED: 1900 mday %d != 1\n", tm_res.tm_mday);
        passed = false;
    }

    // Test 4: Leap year before 1970 (1968-02-29)
    // t = -58060800
    t = -58060800L;
    if (substrate_gmtime_r(&t, &tm_res) == NULL) {
        printf("FAILED: 1968 returned NULL\n");
        return false;
    }
    if (tm_res.tm_year != 68) {
        printf("FAILED: 1968 year %d != 68\n", tm_res.tm_year);
        passed = false;
    }
    if (tm_res.tm_mon != 1) {
        printf("FAILED: 1968 mon %d != 1\n", tm_res.tm_mon);
        passed = false;
    }
    if (tm_res.tm_mday != 29) {
        printf("FAILED: 1968 mday %d != 29\n", tm_res.tm_mday);
        passed = false;
    }

    if (passed) {
        printf("libc_gmtime_r negative years tests passed!\n");
    }
    return passed;
}

int main(void) {
    if (test_gmtime_negative_years()) {
        return 0;
    } else {
        return 1;
    }
}
