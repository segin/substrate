#include <sys/types.h>
#include <sys/times.h>
#include <sys/time.h>
#include <stdio.h>

void test_func(void) {
    struct tms t;
    struct timeval tv;
    struct timezone tz;
    struct itimerval it;

    // Use the types to ensure they are defined
    t.tms_utime = 0;
    tv.tv_sec = 0;
    tz.tz_minuteswest = 0;
    it.it_interval.tv_sec = 0;

    printf("Time structs size: tms=%lu, timeval=%lu\n", 
           sizeof(struct tms), sizeof(struct timeval));
}

int main(void) {
    test_func();
    return 0;
}
