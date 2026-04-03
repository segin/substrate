#include <stdio.h>
#include <time.h>
#include <string.h>
#include <at.h>
#include <assert.h>
#include <stdbool.h>

void test_parse(const char *timespec, bool expected_success) {
    char *argv[16];
    int argc = 0;
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", timespec);

    char *p = strtok(buf, " ");
    while (p && argc < 15) {
        argv[argc++] = p;
        p = strtok(NULL, " ");
    }
    argv[argc] = NULL;

    time_t out_time;
    int res = at_parse_time(argc, argv, 0, &out_time);

    if (expected_success) {
        if (res != 0) {
            fprintf(stderr, "FAIL: '%s' expected success, got %d\n", timespec, res);
            assert(res == 0);
        }
        printf("PASS: '%s' -> %ld\n", timespec, (long)out_time);
    } else {
        if (res == 0) {
            fprintf(stderr, "FAIL: '%s' expected failure, got success (%ld)\n", timespec, (long)out_time);
            assert(res != 0);
        }
        printf("PASS: '%s' failed as expected\n", timespec);
    }
}

int main() {
    printf("Running at_parse_time tests...\n");

    test_parse("now", true);
    test_parse("noon", true);
    test_parse("midnight", true);
    test_parse("teatime", true);
    test_parse("tomorrow", true);

    test_parse("10:00", true);
    test_parse("22:00", true);
    test_parse("1000", true);
    test_parse("now + 1 minute", true);
    test_parse("now + 2 hours", true);
    test_parse("now + 1 day", true);
    test_parse("tomorrow + 1 hour", true);
    test_parse("10:00 + 30 minutes", true);
    test_parse("noon + 1 day", true);
    test_parse("midnight + 1 hour", true);

    // New AM/PM tests
    test_parse("10am", true);
    test_parse("10pm", true);
    test_parse("10:00am", true);
    test_parse("10:00pm", true);
    test_parse("10:00 am", true);
    test_parse("10:00 pm", true);
    test_parse("12:00am", true);
    test_parse("12:00pm", true);
    test_parse("11:59pm", true);
    test_parse("12:01am", true);

    test_parse("invalid_time", false);

    printf("All tests passed!\n");
    return 0;
}
