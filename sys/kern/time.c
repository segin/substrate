#include <stdint.h>
#include <sys/types.h>
#include <sys/math.h>
#include <kern/time.h>

int64_t boot_time = 0;

static uint64_t ticks = 0;
static const uint32_t HZ = 100;

int64_t get_time(void) {
    return boot_time + div64_32(ticks, HZ);
}

int64_t get_uptime(void) {
    return div64_32(ticks, HZ);
}

int64_t get_uptime_ms(void) {
    return ticks * (1000 / HZ);
}

void set_boot_time(int64_t time) {
    boot_time = time;
}

int64_t sys_time(int64_t *tloc) {
    int64_t t = get_time();
    if (tloc) *tloc = t;
    return t;
}

struct timeval {
    int64_t tv_sec;
    int32_t tv_usec;
};

struct timezone {
    int32_t tz_minuteswest;
    int32_t tz_dsttime;
};

int sys_gettimeofday(struct timeval *tv, struct timezone *tz) {
    if (!tv) return -1;
    
    int64_t total_seconds = boot_time + div64_32(ticks, HZ);
    
    tv->tv_sec = total_seconds;
    tv->tv_usec = (int32_t)(div64_32(mod64_32(ticks, HZ) * 1000000, HZ));
    
    if (tz) {
        tz->tz_minuteswest = 0;
        tz->tz_dsttime = 0;
    }
    
    return 0;
}

struct timespec {
    int64_t tv_sec;
    int32_t tv_nsec;
};

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1

int sys_clock_gettime(int clk_id, struct timespec *tp) {
    if (!tp) return -1;
    
    if (clk_id == CLOCK_REALTIME) {
        int64_t total_seconds = boot_time + div64_32(ticks, HZ);
        tp->tv_sec = total_seconds;
        tp->tv_nsec = (int32_t)(div64_32(mod64_32(ticks, HZ) * 1000000000, HZ));
    } else if (clk_id == CLOCK_MONOTONIC) {
        int64_t uptime = div64_32(ticks, HZ);
        tp->tv_sec = uptime;
        tp->tv_nsec = (int32_t)(div64_32(mod64_32(ticks, HZ) * 1000000000, HZ));
    } else {
        return -1;
    }
    
    return 0;
}

struct tms {
    int32_t tms_utime;
    int32_t tms_stime;
    int32_t tms_cutime;
    int32_t tms_cstime;
};

int32_t sys_times(struct tms *buf) {
    if (!buf) return -1;
    buf->tms_utime = 0;
    buf->tms_stime = 0;
    buf->tms_cutime = 0;
    buf->tms_cstime = 0;
    return (int32_t)ticks;
}

void timer_tick(void) {
    ticks++;
}
