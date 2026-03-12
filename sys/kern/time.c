#include <stdint.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/math.h>
#include <time.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/proc.h>
#include <sys/errno.h>
#include <sys/copy.h>
#include <sys/signal.h>
#include <string.h>
#include <kern/sched.h>
#include <pm/pm.h>
#include <arch/i386/percpu.h>
#include <drivers/video/hw_text.h>
#include <sys/kern_syscalls.h>

time_t boot_time = 0;

static uint64_t ticks = 0;

static int proc_itimer_index(int which) {
    switch (which) {
    case ITIMER_REAL:
        return 0;
    case ITIMER_VIRTUAL:
        return 1;
    case ITIMER_PROF:
        return 2;
    default:
        return -1;
    }
}

static int proc_itimer_signal(int which) {
    switch (which) {
    case ITIMER_REAL:
        return SIGALRM;
    case ITIMER_VIRTUAL:
        return SIGVTALRM;
    case ITIMER_PROF:
        return SIGPROF;
    default:
        return 0;
    }
}

static int timeval_is_valid(const struct timeval *tv) {
    return tv && tv->tv_sec >= 0 && tv->tv_usec >= 0 && tv->tv_usec < 1000000;
}

static uint64_t timeval_to_ticks(const struct timeval *tv) {
    uint64_t sec_ticks;
    uint64_t usec_ticks;

    if (!tv || (tv->tv_sec == 0 && tv->tv_usec == 0)) {
        return 0;
    }

    sec_ticks = (uint64_t)tv->tv_sec * HZ;
    usec_ticks = ((uint64_t)tv->tv_usec * HZ + 999999ULL) / 1000000ULL;

    if (sec_ticks == 0 && usec_ticks == 0) {
        return 1;
    }
    return sec_ticks + usec_ticks;
}

static void ticks_to_timeval(uint64_t tick_count, struct timeval *tv) {
    if (!tv) {
        return;
    }
    tv->tv_sec = (time_t)(tick_count / HZ);
    tv->tv_usec = (suseconds_t)(((tick_count % HZ) * 1000000ULL) / HZ);
}

static void proc_getitimer_locked(process_t *p, int idx, struct itimerval *curr_value) {
    if (!curr_value) {
        return;
    }
    memset(curr_value, 0, sizeof(*curr_value));
    ticks_to_timeval(p->itimer_interval_ticks[idx], &curr_value->it_interval);
    ticks_to_timeval(p->itimer_value_ticks[idx], &curr_value->it_value);
}

static int proc_timer_fire(process_t *p, int which) {
    int idx = proc_itimer_index(which);
    int signal = proc_itimer_signal(which);
    int fire = 0;

    if (!p || idx < 0 || signal == 0) {
        return 0;
    }

    spinlock_acquire(&p->itimer_lock);
    if (p->itimer_value_ticks[idx] > 0) {
        p->itimer_value_ticks[idx]--;
        if (p->itimer_value_ticks[idx] == 0) {
            fire = 1;
            if (p->itimer_interval_ticks[idx] > 0) {
                p->itimer_value_ticks[idx] = p->itimer_interval_ticks[idx];
            }
        }
    }
    spinlock_release(&p->itimer_lock);

    if (fire && p->state != SDYING && p->state != SZOMB) {
        psignal(p, signal);
    }

    return fire;
}

void proc_timers_init(process_t *p) {
    if (!p) {
        return;
    }
    memset(p->itimer_value_ticks, 0, sizeof(p->itimer_value_ticks));
    memset(p->itimer_interval_ticks, 0, sizeof(p->itimer_interval_ticks));
    spinlock_init(&p->itimer_lock, "itimer");
}

void proc_timers_cancel(process_t *p) {
    if (!p) {
        return;
    }
    spinlock_acquire(&p->itimer_lock);
    memset(p->itimer_value_ticks, 0, sizeof(p->itimer_value_ticks));
    memset(p->itimer_interval_ticks, 0, sizeof(p->itimer_interval_ticks));
    spinlock_release(&p->itimer_lock);
}

uint64_t get_ticks(void) {
    return ticks;
}

time_t get_time(void) {
    return boot_time + (ticks / HZ);
}

time_t get_uptime(void) {
    return ticks / HZ;
}

int64_t get_uptime_ms(void) {
    return (ticks * 1000) / HZ;
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

int kern_stime(time_t *t) {
    if (!t) return -1;
    boot_time = *t - (ticks / HZ);
    return 0;
}
int kern_gettimeofday(struct timeval *tv, struct timezone *tz) {
    if (!tv) return -1;
    
    time_t total_seconds = boot_time + (ticks / HZ);
    
    tv->tv_sec = total_seconds;
    tv->tv_usec = (suseconds_t)(((ticks % HZ) * 1000000) / HZ);
    
    if (tz) {
        tz->tz_minuteswest = 0;
        tz->tz_dsttime = 0;
    }
    
    return 0;
}

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#endif

int kern_clock_gettime(clockid_t clk_id, struct timespec *tp) {
    if (!tp) return -1;
    
    if (clk_id == CLOCK_REALTIME) {
        time_t total_seconds = boot_time + (ticks / HZ);
        tp->tv_sec = total_seconds;
        tp->tv_nsec = (long)(((ticks % HZ) * 1000000000) / HZ);
    } else if (clk_id == CLOCK_MONOTONIC) {
        time_t uptime = ticks / HZ;
        tp->tv_sec = uptime;
        tp->tv_nsec = (long)(((ticks % HZ) * 1000000000) / HZ);
    } else {
        return -1;
    }
    
    return 0;
}

clock_t kern_times(struct tms *buf) {
    if (!buf) return (clock_t)-1;
    buf->tms_utime = 0;
    buf->tms_stime = 0;
    buf->tms_cutime = 0;
    buf->tms_cstime = 0;
    return (clock_t)ticks;
}

unsigned int kern_alarm(unsigned int sec) {
    struct itimerval new_value;
    struct itimerval old_value;
    unsigned int remaining;

    memset(&new_value, 0, sizeof(new_value));
    new_value.it_value.tv_sec = (time_t)sec;

    if (kern_setitimer(ITIMER_REAL, &new_value, &old_value) != 0) {
        return 0;
    }

    remaining = (unsigned int)old_value.it_value.tv_sec;
    if (old_value.it_value.tv_usec != 0) {
        remaining++;
    }
    return remaining;
}

int kern_getitimer(int which, struct itimerval *curr_value) {
    int idx = proc_itimer_index(which);

    if (!current_process || !curr_value || idx < 0) {
        return -EINVAL;
    }

    spinlock_acquire(&current_process->itimer_lock);
    proc_getitimer_locked(current_process, idx, curr_value);
    spinlock_release(&current_process->itimer_lock);
    return 0;
}

int kern_setitimer(int which, const struct itimerval *new_value, struct itimerval *old_value) {
    int idx = proc_itimer_index(which);

    if (!current_process || !new_value || idx < 0) {
        return -EINVAL;
    }
    if (!timeval_is_valid(&new_value->it_interval) || !timeval_is_valid(&new_value->it_value)) {
        return -EINVAL;
    }

    spinlock_acquire(&current_process->itimer_lock);
    if (old_value) {
        proc_getitimer_locked(current_process, idx, old_value);
    }
    current_process->itimer_interval_ticks[idx] = timeval_to_ticks(&new_value->it_interval);
    current_process->itimer_value_ticks[idx] = timeval_to_ticks(&new_value->it_value);
    spinlock_release(&current_process->itimer_lock);
    return 0;
}

time_t sys_time(time_t *tloc) {
    time_t t = kern_time(NULL);
    if (tloc) {
        if (copyout(&t, tloc, sizeof(time_t)) != 0) return -14;
    }
    return (int)t;
}

int sys_stime(time_t *t) {
    time_t kt;
    if (copyin(t, &kt, sizeof(time_t)) != 0) return -14;
    return kern_stime(&kt);
}

int sys_gettimeofday(struct timeval *tv, struct timezone *tz) {
    struct timeval ktv;
    struct timezone ktz;
    int ret = kern_gettimeofday(&ktv, tz ? &ktz : NULL);
    if (ret == 0) {
        if (tv && copyout(&ktv, tv, sizeof(struct timeval)) != 0) return -14;
        if (tz && copyout(&ktz, tz, sizeof(struct timezone)) != 0) return -14;
    }
    return ret;
}

int sys_clock_gettime(clockid_t clk_id, struct timespec *tp) {
    struct timespec ktp;
    int ret = kern_clock_gettime(clk_id, &ktp);
    if (ret == 0) {
        if (copyout(&ktp, tp, sizeof(struct timespec)) != 0) return -14;
    }
    return ret;
}

clock_t sys_times(struct tms *buf) {
    struct tms ktms;
    clock_t ret = kern_times(&ktms);
    if (ret != (clock_t)-1) {
        if (copyout(&ktms, buf, sizeof(struct tms)) != 0) return (clock_t)-1;
    }
    return ret;
}

unsigned int sys_alarm(unsigned int sec) {
    return kern_alarm(sec);
}

int sys_getitimer(int which, struct itimerval *curr_value) {
    struct itimerval kcurr_value;
    int ret;

    if (!curr_value) {
        return -EFAULT;
    }

    ret = kern_getitimer(which, &kcurr_value);
    if (ret == 0 && copyout(&kcurr_value, curr_value, sizeof(kcurr_value)) != 0) {
        return -EFAULT;
    }
    return ret;
}

int sys_setitimer(int which, const struct itimerval *new_value, struct itimerval *old_value) {
    struct itimerval knew_value;
    struct itimerval kold_value;
    int ret;

    if (!new_value) {
        return -EFAULT;
    }
    if (copyin(new_value, &knew_value, sizeof(knew_value)) != 0) {
        return -EFAULT;
    }

    ret = kern_setitimer(which, &knew_value, old_value ? &kold_value : NULL);
    if (ret == 0 && old_value && copyout(&kold_value, old_value, sizeof(kold_value)) != 0) {
        return -EFAULT;
    }
    return ret;
}

void timer_tick_context(int is_usermode) {
    ticks++;
    sched_tick();
    if ((ticks % (5 * HZ)) == 0) {
        sched_update_loadavg();
    }

    if (percpu_get_cpu_id() == 0) {
        if ((ticks % HZ) == 0) {
            hw_text_tick_1hz();
        }
        for (int i = 0; i < MAX_PROCS; i++) {
            process_t *p = &processes[i];
            if (p->pid == -1 || p->is_kernel_task) {
                continue;
            }
            proc_timer_fire(p, ITIMER_REAL);
        }
    }

    if (current_process && current_process->pid != -1 && !current_process->is_kernel_task) {
        if (is_usermode) {
            proc_timer_fire(current_process, ITIMER_VIRTUAL);
        }
        proc_timer_fire(current_process, ITIMER_PROF);
    }
}

void timer_tick(void) {
    timer_tick_context(0);
}
