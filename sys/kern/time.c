#include <stdint.h>
#include <sys/types.h>
#include <sys/math.h>
#include <time.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/signal.h>
#include <kern/time.h>
#include <kern/sched.h>

time_t boot_time = 0;

static uint64_t ticks = 0;
static const uint32_t HZ = 100;

uint64_t get_ticks(void) {
    return ticks;
}

time_t get_time(void) {
    return boot_time + div64_32(ticks, HZ);
}

time_t get_uptime(void) {
    return div64_32(ticks, HZ);
}

int64_t get_uptime_ms(void) {
    return ticks * (1000 / HZ);
}

uint32_t get_hz(void) {
    return HZ;
}

void set_boot_time(time_t time) {
    boot_time = time;
}

time_t kern_time(time_t *tloc) {
    time_t t = get_time();
    if (tloc) *tloc = t;
    return t;
}

time_t sys_time(time_t *tloc) {
    time_t t;
    kern_time(&t);
    if (tloc) {
        if (copyout(&t, tloc, sizeof(time_t)) != 0) return -14;
    }
    return t;
}

int kern_stime(time_t *t) {
    if (!t) return -1;
    boot_time = *t - div64_32(ticks, HZ);
    return 0;
}

int sys_stime(time_t *t) {
    time_t kt;
    if (copyin(t, &kt, sizeof(time_t)) != 0) return -14;
    return kern_stime(&kt);
}

int kern_gettimeofday(struct timeval *tv, struct timezone *tz) {
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

int sys_gettimeofday(struct timeval *tv, struct timezone *tz) {
    struct timeval ktv;
    struct timezone ktz;
    int ret = kern_gettimeofday(tv ? &ktv : NULL, tz ? &ktz : NULL);
    if (ret == 0) {
        if (tv && copyout(&ktv, tv, sizeof(struct timeval)) != 0) return -14;
        if (tz && copyout(&ktz, tz, sizeof(struct timezone)) != 0) return -14;
    }
    return ret;
}

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#endif

int kern_clock_gettime(clockid_t clk_id, struct timespec *tp) {
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

int sys_clock_gettime(clockid_t clk_id, struct timespec *tp) {
    struct timespec ktp;
    int ret = kern_clock_gettime(clk_id, &ktp);
    if (ret == 0) {
        if (copyout(&ktp, tp, sizeof(struct timespec)) != 0) return -14;
    }
    return ret;
}

clock_t kern_times(struct tms *buf) {
    if (!buf) return (clock_t)-1;
    buf->tms_utime = 0;
    buf->tms_stime = 0;
    buf->tms_cutime = 0;
    buf->tms_cstime = 0;
    return (clock_t)ticks;
}

clock_t sys_times(struct tms *buf) {
    struct tms kbuf;
    clock_t ret = kern_times(&kbuf);
    if (ret != (clock_t)-1) {
        if (copyout(&kbuf, buf, sizeof(struct tms)) != 0) return (clock_t)-14;
    }
    return ret;
}

void timer_tick(void) {
    ticks++;
    sched_tick();
    if (mod64_32(ticks, 5 * HZ) == 0) {
        sched_update_loadavg();
    }
}
