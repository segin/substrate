#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <sys/times.h>
#include <sys/time.h>
#include <assert.h>

// Mock data
static clock_t mock_utime = 0;
static clock_t mock_stime = 0;
static clock_t mock_return = 0;

// Rename times to mock_times for the call inside time.c
#define times mock_times
// Rename clock to tested_clock to avoid conflict with standard clock()
#define clock tested_clock

clock_t mock_times(struct tms *buf) {
    buf->tms_utime = mock_utime;
    buf->tms_stime = mock_stime;
    buf->tms_cutime = 0;
    buf->tms_cstime = 0;
    return mock_return;
}

// Include the source file
// We assume this runs from repo root, so relative path is needed
#include "../src/time/time.c"

#undef times
#undef clock

int main(void) {
    // Ensure CLOCKS_PER_SEC is as expected for the test logic (1,000,000)
    // If host differs, we might need to adjust expectations, but time.c uses CLOCKS_PER_SEC from the header it sees.
    // Here we see host header.

    printf("CLOCKS_PER_SEC = %ld\n", (long)CLOCKS_PER_SEC);

    // Test case 1: 0 ticks
    mock_utime = 0;
    mock_stime = 0;
    mock_return = 0;

    clock_t res = tested_clock();
    printf("0 ticks -> %lu\n", (unsigned long)res);
    assert(res == 0);

    // Test case 2: 100 ticks
    // Our implementation assumes 100Hz tickrate.
    // So 100 ticks = 1 second.
    // Result should be 1 * CLOCKS_PER_SEC.
    mock_utime = 60;
    mock_stime = 40;

    res = tested_clock();
    printf("100 ticks -> %lu (Expected %lu)\n", (unsigned long)res, (unsigned long)CLOCKS_PER_SEC);

    assert(res == CLOCKS_PER_SEC);

    // Test case 3: 200 ticks
    mock_utime = 150;
    mock_stime = 50;
    res = tested_clock();
    printf("200 ticks -> %lu\n", (unsigned long)res);
    assert(res == 2 * CLOCKS_PER_SEC);

    // Test case 4: Error
    mock_return = (clock_t)-1;
    res = tested_clock();
    assert(res == (clock_t)-1);

    printf("Clock logic test passed.\n");
    return 0;
}
