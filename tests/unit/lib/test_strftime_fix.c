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

bool test_strftime_percent(void) {
    struct tm tm_buf;
    char buf[64];
    memset(&tm_buf, 0, sizeof(tm_buf));

    if (our_strftime(buf, sizeof(buf), "%%", &tm_buf) == 0) {
        printf("FAILED: strftime returned 0 for %%\n");
        return false;
    }
    if (strcmp(buf, "%") != 0) {
        printf("FAILED: strftime %% got '%s', expected '%%'\n", buf);
        return false;
    }
    return true;
}

bool test_strftime_boundary(void) {
    struct tm tm_buf;
    char buf[4];
    memset(&tm_buf, 0, sizeof(tm_buf));
    tm_buf.tm_year = 123; // 2023

    // format "2023" needs 5 bytes (including null)
    // we give 4
    size_t ret = our_strftime(buf, 4, "%Y", &tm_buf);
    if (ret != 0 && strcmp(buf, "2023") == 0) {
         // Current implementation seems to just truncate if it doesn't fit?
         // Let's re-read the code.
         /*
        if (count + len < maxsize - 1) {
            strcpy(s + count, tmp);
            count += len;
        }
        ...
        s[count] = 0;
        */
        // If it doesn't fit, it just skips it.
    }

    // Let's test if it overflows
    char big_buf[10];
    memset(big_buf, 'A', sizeof(big_buf));
    our_strftime(big_buf, 3, "%Y", &tm_buf);
    if (big_buf[3] != 'A') {
        printf("FAILED: strftime boundary overflow\n");
        return false;
    }

    return true;
}

int main() {
    bool p1 = test_strftime_percent();
    bool p2 = test_strftime_boundary();
    if (p1 && p2) {
        printf("PASS\n");
        return 0;
    } else {
        printf("FAIL\n");
        return 1;
    }
}
