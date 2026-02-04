#include <stdint.h>
#include <sys/types.h>
#include <sys/math.h>
#include <sys/times.h>
#include <kern/time.h>

time_t boot_time = 0;

static uint64_t ticks = 0;
static const uint32_t HZ = 100;

time_t get_time(void) {
    return boot_time + div64_32(ticks, HZ);
}

time_t get_uptime(void) {
    return div64_32(ticks, HZ);
}

int64_t get_uptime_ms(void) {
    return ticks * (1000 / HZ);
}

void set_boot_time(time_t time) {
    boot_time = time;
}

time_t sys_time(time_t *tloc) {
    time_t t = get_time();
    if (tloc) *tloc = t;
    return t;
}

int sys_stime(time_t *t) {
    if (!t) return -1;
    boot_time = *t - div64_32(ticks, HZ);
    return 0;
}

struct timeval {
    time_t tv_sec;
    suseconds_t tv_usec;
};

struct timezone {
    int32_t tz_minuteswest;
    int32_t tz_dsttime;
};

int sys_gettimeofday(struct timeval *tv, struct timezone *tz) {
    if (!tv) return -1;
    
    time_t total_seconds = boot_time + div64_32(ticks, HZ);
    
    tv->tv_sec = total_seconds;
    tv->tv_usec = (suseconds_t)(div64_32(mod64_32(ticks, HZ) * 1000000, HZ));
    
    if (tz) {
        tz->tz_minuteswest = 0;
        tz->tz_dsttime = 0;
    }
    
    return 0;
}

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1

int sys_clock_gettime(clockid_t clk_id, struct timespec *tp) {
    if (!tp) return -1;
    
    if (clk_id == CLOCK_REALTIME) {
        time_t total_seconds = boot_time + div64_32(ticks, HZ);
        tp->tv_sec = total_seconds;
        tp->tv_nsec = (long)(div64_32(mod64_32(ticks, HZ) * 1000000000, HZ));
    } else if (clk_id == CLOCK_MONOTONIC) {
        time_t uptime = div64_32(ticks, HZ);
        tp->tv_sec = uptime;
        tp->tv_nsec = (long)(div64_32(mod64_32(ticks, HZ) * 1000000000, HZ));
    } else {
        return -1;
    }
    
    return 0;
}

clock_t sys_times(struct tms *buf) {
    if (!buf) return (clock_t)-1;
    buf->tms_utime = 0;
    buf->tms_stime = 0;
    buf->tms_cutime = 0;
    buf->tms_cstime = 0;
    return (clock_t)ticks;
}

void timer_tick(void) {
    ticks++;
}
