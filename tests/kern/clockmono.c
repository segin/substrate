/*
 * clockmono -- CLOCK_MONOTONIC never goes backwards.
 *
 * The kernel built the time from the tick count and a TSC snapshot taken
 * at the last tick, read one after the other; a tick landing between the
 * two reads paired the old count with the new snapshot, a time almost a
 * whole tick (4 ms) in the past.  torture_kernel's mutex_fair saw it as
 * acquire gaps of 2^64 - 4000 us.  Three processes each read
 * CLOCK_MONOTONIC (and gettimeofday) in a tight loop for 2 s -- thousands
 * of tick boundaries -- and count any step backwards.  Portable POSIX.
 */
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static uint64_t mono_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static uint64_t tod_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

static int reader(void)
{
    uint64_t start = mono_ns(), prev = start, prev_tod = tod_us();
    long back = 0, back_tod = 0, reads = 0;
    uint64_t worst = 0;

    while (prev - start < 2000000000ULL) {
        uint64_t t = mono_ns();
        if (t < prev) {
            back++;
            if (prev - t > worst)
                worst = prev - t;
        }
        prev = t > prev ? t : prev;
        uint64_t u = tod_us();
        if (u < prev_tod)
            back_tod++;
        prev_tod = u > prev_tod ? u : prev_tod;
        reads++;
    }
    if (back || back_tod) {
        printf("clockmono: %ld of %ld CLOCK_MONOTONIC reads went backwards "
               "(worst %llu ns); gettimeofday %ld\n", back, reads,
               (unsigned long long)worst, back_tod);
        return 1;
    }
    return 0;
}

int main(void)
{
    int fails = 0;

    for (int i = 0; i < 3; i++)
        if (fork() == 0)
            _exit(reader());
    for (int i = 0; i < 3; i++) {
        int st = 0;
        wait(&st);
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0)
            fails++;
    }
    printf("clockmono: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
