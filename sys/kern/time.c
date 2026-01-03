#include <stdint.h>
#include <sys/types.h>
#include "time.h"

// Boot time (seconds since epoch) - set by RTC driver
int64_t boot_time = 0;

// Uptime tracking
static uint64_t ticks = 0;      // Total ticks since boot
static const uint32_t HZ = 100; // Timer frequency (100Hz)

// Software 64-bit division by 32-bit constant
// Avoids linking against __divdi3 from libgcc
static inline int64_t div64_32(int64_t dividend, uint32_t divisor) {
    int64_t quotient = 0;
    int64_t remainder = 0;
    
    // Handle negative dividends
    int negative = 0;
    if (dividend < 0) {
        negative = 1;
        dividend = -dividend;
    }
    
    // Long division algorithm
    for (int i = 63; i >= 0; i--) {
        remainder <<= 1;
        remainder |= (dividend >> i) & 1;
        
        if ((uint64_t)remainder >= divisor) {
            remainder -= divisor;
            quotient |= (1LL << i);
        }
    }
    
    return negative ? -quotient : quotient;
}

int64_t get_uptime(void) {
    return div64_32((int64_t)ticks, HZ);
}

int64_t get_time(void) {
    return boot_time + get_uptime();
}

// Syscall stub
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
    if (tv) {
        int64_t now = get_time();
        tv->tv_sec = now;
        // Calculate microseconds from remaining ticks
        uint64_t remaining_ticks = ticks % HZ;
        tv->tv_usec = (int32_t)((remaining_ticks * 1000000) / HZ);
    }
    if (tz) {
        tz->tz_minuteswest = 0;  // UTC
        tz->tz_dsttime = 0;      // No DST
    }
    return 0;
}

// clock_gettime syscall
struct timespec {
    int64_t tv_sec;
    int32_t tv_nsec;
};

int sys_clock_gettime(int clk_id, struct timespec *tp) {
    if (!tp) return -1;
    
    if (clk_id == 0) {  // CLOCK_REALTIME
        int64_t now = get_time();
        tp->tv_sec = now;
        uint64_t remaining_ticks = ticks % HZ;
        tp->tv_nsec = (int32_t)((remaining_ticks * 1000000000) / HZ);
    } else if (clk_id == 1) {  // CLOCK_MONOTONIC
        int64_t uptime = get_uptime();
        tp->tv_sec = uptime;
        uint64_t remaining_ticks = ticks % HZ;
        tp->tv_nsec = (int32_t)((remaining_ticks * 1000000000) / HZ);
    } else {
        return -1;  // Unsupported clock
    }
    return 0;
}

// times syscall (process time tracking)
struct tms {
    int32_t tms_utime;   // user time
    int32_t tms_stime;   // system time
    int32_t tms_cutime;  // user time of children
    int32_t tms_cstime;  // system time of children
};

int32_t sys_times(struct tms *buf) {
    if (buf) {
        // For now, return zeroes - proper accounting needs scheduler integration
        buf->tms_utime = 0;
        buf->tms_stime = 0;
        buf->tms_cutime = 0;
        buf->tms_cstime = 0;
    }
    // Return current uptime in clock ticks
    return (int32_t)ticks;
}

// Tick handler (called from timer interrupt)
void timer_tick(void) {
    ticks++;
}
