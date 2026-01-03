#include "time.h"
#include "../arch/i386/io.h"
#include <stdint.h>

volatile int64_t ticks = 0;
int64_t boot_time = 0;  // Unix timestamp when system booted

#define HZ 100  // Timer frequency (ticks per second)

// Software 64-bit division to avoid libgcc dependency
static uint64_t div64_32(uint64_t dividend, uint32_t divisor) {
    if (divisor == 0) return 0;
    
    uint64_t quotient = 0;
    uint64_t remainder = 0;
    
    for (int i = 63; i >= 0; i--) {
        remainder = (remainder << 1) | ((dividend >> i) & 1);
        if (remainder >= divisor) {
            remainder -= divisor;
            quotient |= (1ULL << i);
        }
    }
    
    return quotient;
}

// Software 64-bit modulo
static uint64_t mod64_32(uint64_t dividend, uint32_t divisor) {
    if (divisor == 0) return 0;
    
    uint64_t quotient = div64_32(dividend, divisor);
    return dividend - (quotient * divisor);
}

int64_t get_time(void) {
    return boot_time + div64_32(ticks, HZ);
}

int64_t get_uptime(void) {
    return div64_32(ticks, HZ);
}

void set_boot_time(int64_t time) {
    boot_time = time;
}

// time syscall
int64_t sys_time(int64_t *tloc) {
    int64_t t = get_time();
    if (tloc) *tloc = t;
    return t;
}

// gettimeofday syscall
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

// clock_gettime syscall
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

// times syscall  
struct tms {
    int32_t tms_utime;
    int32_t tms_stime;
    int32_t tms_cutime;
    int32_t tms_cstime;
};

int32_t sys_times(struct tms *buf) {
    if (!buf) return -1;
    
    // TODO: Track actual process/thread times
    buf->tms_utime = 0;
    buf->tms_stime = 0;
    buf->tms_cutime = 0;
    buf->tms_cstime = 0;
    
    return (int32_t)ticks;
}

void timer_tick(void) {
    ticks++;
}
