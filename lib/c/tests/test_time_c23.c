#include <time.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

void test_formatting(void) {
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = 123; // 2023
    t.tm_mon = 0;    // Jan
    t.tm_mday = 15;
    t.tm_hour = 12;
    t.tm_min = 34;
    t.tm_sec = 56;
    t.tm_wday = 0;   // Sun

    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
    printf("Formatted: %s\n", buf);
    assert(strcmp(buf, "2023-01-15 12:34:56") == 0);

    // Test reentrant functions
    char asctime_buf[26];
    asctime_r(&t, asctime_buf);
    printf("Asctime: %s", asctime_buf);
    // Standard format: "Sun Jan 15 12:34:56 2023\n"
    assert(strstr(asctime_buf, "Jan 15 12:34:56 2023") != NULL);
}

void test_mktime_gmtime(void) {
    time_t now = 1672531200; // 2023-01-01 00:00:00 UTC
    struct tm res;
    gmtime_r(&now, &res);
    
    printf("Gmtime: %d-%d-%d\n", res.tm_year + 1900, res.tm_mon + 1, res.tm_mday);
    assert(res.tm_year == 123);
    assert(res.tm_mon == 0);
    assert(res.tm_mday == 1);

    time_t back = mktime(&res);
    printf("Mktime: %lld vs %lld\n", (long long)back, (long long)now);
    assert(back == now);
}

int main(void) {
    test_formatting();
    test_mktime_gmtime();
    printf("C23 time.h compliance tests passed.\n");
    return 0;
}
