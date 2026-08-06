#include <stdint.h>
#include <string.h>

#include <arch/i386/cpu.h>
#include <arch/i386/intr.h>
#include <arch/i386/percpu.h>
#include <arch/x86-common/io.h>
#include <drivers/storage/floppy/floppy.h>
#include <drivers/video/fb_console.h>
#include <drivers/video/hw_text.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <pm/pm.h>
#include <sys/copy.h>
#include <sys/errno.h>
#include <sys/kern_syscalls.h>
#include <sys/math.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/signal.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/vt.h>
#include <time.h>

time_t boot_time = 0;

static uint64_t ticks = 0;

/*
 * Wall-clock seconds maintained incrementally from the tick, so get_time()/
 * get_uptime() never perform a 64-bit divide (ticks / HZ).  On i386 there is
 * no hardware 64-bit division, so `ticks / HZ` compiles to __udivdi3 (~100+
 * cycles); get_time() is called on every VFS write (write_fs stamps mtime),
 * so the X server's socket writes alone were spending the bulk of a CPU in
 * software division.  uptime_secs is bumped once per HZ ticks below.
 */
static uint64_t uptime_secs = 0;
static uint32_t tick_in_sec = 0;

/*
 * TSC-based sub-tick interpolation.  HZ is 128 (≈7.8 ms per tick); without
 * a finer source any RTT shorter than a tick quantizes to a multiple of
 * 7.8 ms and gettimeofday's tv_usec is useless for things like ping(8).
 * We calibrate the TSC against the PIT at boot, then in the timer ISR
 * snapshot `tsc_at_last_tick` so gettimeofday / clock_gettime can add
 * `(rdtsc() - tsc_at_last_tick) / tsc_hz` worth of nanoseconds on top of
 * the tick-aligned base.
 */
static uint64_t tsc_hz             = 0;       /* 0 = uncalibrated, fall back */
static uint64_t tsc_at_last_tick   = 0;
static uint64_t tsc_ns_per_tick    = 0;       /* 1e9 / HZ, precomputed */



#define PIT_FREQUENCY 1193182U
#define PIT_CHANNEL0  0x40U
#define PIT_CHANNEL2  0x42U
#define PIT_COMMAND   0x43U
#define PIT_MODE_RATE_GENERATOR 0x34U
/* PIT channel 2 one-shot, lobyte/hibyte access, mode 0 (interrupt on
 * terminal count), binary count.  Gated by NMI status/ctrl port 0x61 bit 0. */
#define PIT_MODE2_ONESHOT       0xB0U
#define NMI_STATUS_CONTROL      0x61U

/*
 * Calibrate TSC frequency by polling PIT channel 2 for a known interval.
 * Called once at timer_init.  Result in `tsc_hz`; 0 means calibration
 * failed and we'll skip the interpolation.
 */
static void tsc_calibrate(void) {
    if (!i386_cpu_has_tsc()) return;

    /* Program PIT ch2 for a one-shot of ~50ms (calibrate_us microseconds).
     * Count = PIT_FREQUENCY * us / 1_000_000.  50ms is large enough to
     * dominate sampling jitter and small enough that the count fits in
     * 16 bits (50000us * 1193182 / 1e6 = 59659 — fits). */
    const uint32_t calibrate_us = 50000;
    uint32_t count = (uint32_t)((uint64_t)PIT_FREQUENCY * calibrate_us / 1000000U);
    if (count == 0 || count > 0xFFFFU) return;

    /* Disable speaker, enable PIT2 gate */
    uint8_t prev = inb(NMI_STATUS_CONTROL);
    outb(NMI_STATUS_CONTROL, (uint8_t)((prev & ~0x02U) | 0x01U));

    outb(PIT_COMMAND, PIT_MODE2_ONESHOT);
    outb(PIT_CHANNEL2, (uint8_t)(count & 0xFFU));
    outb(PIT_CHANNEL2, (uint8_t)((count >> 8) & 0xFFU));

    uint64_t tsc_start = i386_cpu_cycle_counter();
    /* Wait for OUT2 (bit 5 of port 0x61) to go high — terminal count reached. */
    while ((inb(NMI_STATUS_CONTROL) & 0x20U) == 0) {
        /* spin */
    }
    uint64_t tsc_end = i386_cpu_cycle_counter();

    /* Restore the NMI/SC port. */
    outb(NMI_STATUS_CONTROL, prev);

    uint64_t cycles = tsc_end - tsc_start;
    /* tsc_hz = cycles / (calibrate_us / 1e6) = cycles * 1e6 / calibrate_us */
    tsc_hz = (cycles * 1000000ULL) / calibrate_us;
    tsc_ns_per_tick = 1000000000ULL / HZ;
    tsc_at_last_tick = i386_cpu_cycle_counter();

    kprintf("timer: TSC calibrated at %u MHz (%llu Hz)\n",
            (unsigned)(tsc_hz / 1000000ULL),
            (unsigned long long)tsc_hz);
}

/*
 * Millisecond busy-wait that does not depend on interrupts.
 *
 * get_uptime_ms() counts timer-tick interrupts, so it cannot be used to pace
 * anything that runs with interrupts disabled -- and much of kernel init
 * does, which is why a tick-based version of the console "slow" option
 * produced no delay at all during boot.
 *
 * Polls PIT channel 2 the same way tsc_calibrate() above does.  Channel 2 is
 * not the system tick (that is channel 0), so borrowing it does not disturb
 * timekeeping; its normal job is the PC speaker, idle during boot.  The count
 * is 16-bit, capping a single shot near 54ms, so longer waits are split.
 *
 * The spin is bounded: on hardware where OUT2 never rises this returns early
 * rather than hanging the machine.
 */
void timer_busywait_ms(unsigned ms) {
    while (ms > 0) {
        unsigned chunk = (ms > 50U) ? 50U : ms;
        uint32_t count = (uint32_t)(((uint64_t)PIT_FREQUENCY * chunk) / 1000U);
        uint32_t guard = 0;
        uint8_t prev;

        if (count == 0 || count > 0xFFFFU) {
            return;
        }

        prev = inb(NMI_STATUS_CONTROL);
        outb(NMI_STATUS_CONTROL, (uint8_t)((prev & ~0x02U) | 0x01U));
        outb(PIT_COMMAND, PIT_MODE2_ONESHOT);
        outb(PIT_CHANNEL2, (uint8_t)(count & 0xFFU));
        outb(PIT_CHANNEL2, (uint8_t)((count >> 8) & 0xFFU));

        /*
         * Bound the poll.  Each iteration is a port read costing on the order
         * of a microsecond on real hardware, so the ceiling has to be sized in
         * reads, not in "a big number": at 100 million this took about a
         * hundred seconds per chunk when OUT2 never rose, which does not look
         * like a slow machine, it looks like a dead one.  A 50ms chunk needs
         * roughly 50k reads, so this is several times the expected count and
         * still gives up in a fraction of a second.
         */
        while ((inb(NMI_STATUS_CONTROL) & 0x20U) == 0) {
            if (++guard > 200000U) {
                break;      /* no usable PIT: give up rather than spin on */
            }
        }
        outb(NMI_STATUS_CONTROL, prev);

        ms -= chunk;
    }
}

/*
 * Sub-tick nanoseconds since the most recent timer tick, computed from
 * the TSC.  Returns 0 if calibration hasn't run or TSC isn't available.
 * Clamped to one tick's worth of nanoseconds so a missed snapshot
 * doesn't make tv_nsec overflow into next-second territory.
 */
static uint64_t subtick_nsec(void) {
    if (tsc_hz == 0) return 0;
    uint64_t snap = tsc_at_last_tick;
    uint64_t now  = i386_cpu_cycle_counter();
    if (now <= snap) return 0;
    uint64_t delta = now - snap;
    /* ns = delta * 1e9 / tsc_hz; avoid overflow by using a slightly
     * lossier scaled form. */
    uint64_t ns = (delta / (tsc_hz / 1000000ULL + 1)) * 1000ULL;
    if (ns >= tsc_ns_per_tick) ns = tsc_ns_per_tick - 1;
    return ns;
}

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

    /* Clamp to prevent overflow: UINT64_MAX / HZ is the max safe tv_sec */
    if ((uint64_t)tv->tv_sec > UINT64_MAX / HZ) {
        return UINT64_MAX;
    }

    sec_ticks = (uint64_t)tv->tv_sec * HZ;
    usec_ticks = ((uint64_t)tv->tv_usec * HZ + 999999ULL) / 1000000ULL;

    if (sec_ticks == 0 && usec_ticks == 0) {
        return 1;
    }
    /* Saturate on overflow */
    if (sec_ticks > UINT64_MAX - usec_ticks) {
        return UINT64_MAX;
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

    /*
     * Lockless fast-path.  timer_tick_context() calls this for EVERY process
     * (FOREACH_PROC) on every HZ tick, but the overwhelming majority have no
     * armed ITIMER_REAL/VIRTUAL.  Taking p->itimer_lock is not free: the
     * spinlock acquire and release each issue an MMIO LAPIC read (lapic_get_id,
     * for the owner-cpu id), which is expensive — especially under emulation.
     * Under a fork storm (e.g. OPTS shm_open/23-1 forks 1000 procs) that is
     * thousands of MMIO reads per tick, so the tick handler cannot complete
     * before the next tick fires and the machine livelocks (serial silent, no
     * forward progress — diagnosed via the qemu gdb stub: PC pinned in
     * lapic_read, reached from proc_timer_fire's spinlock ops, for every one of
     * the 1000 timerless procs).  A disarmed timer has itimer_value_ticks[idx]
     * == 0; read it locklessly and bail before the lock.  The read races only
     * with a concurrent setitimer arming the timer, in which case the next tick
     * picks it up — identical to the "skip this tick" behaviour of the
     * try_acquire path below.  Mirrors the n_ptimers_armed fast-path that
     * proc_ptimers_fire() already uses.
     */
    if (p->itimer_value_ticks[idx] == 0) {
        return 0;
    }

    /*
     * Called from timer IRQ context.  If the syscall path on the SAME CPU
     * (e.g. proc_exit -> proc_timers_cancel, sys_setitimer) is mid-update
     * and holds itimer_lock, our blocking acquire would deadlock — the
     * spinlock detects same-CPU re-entry and panics.  Use try_acquire and
     * skip this tick; the next tick will pick the timer up cleanly because
     * itimer_value_ticks/interval are not stale across that single skip.
     */
    if (!spinlock_try_acquire(&p->itimer_lock)) {
        return 0;
    }
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
    /* POSIX timers are per-process and NOT inherited across fork() —
     * proc_create() (used for both the initial process and fork children)
     * calls us, so clearing the table here gives every child an empty set. */
    proc_ptimers_clear(p);
}

/*
 * proc_ptimers_clear - Disarm and free every POSIX per-process timer.
 *
 * Used at process creation (incl. fork children, so timers are not
 * inherited), at exec() (POSIX: timers are deleted across exec), and at
 * exit().  Takes itimer_lock with IRQs disabled: the timer tick reads/writes
 * this table under the same lock from IRQ context, so an unlocked clear on a
 * LIVE, tick-visible process (the exec path — exec_reset_signals) could race
 * the tick into a torn read and fire a stale timer signal into the new image.
 * proc_timers_init() always spinlock_init()s itimer_lock before calling us, so
 * the lock is valid on every caller.  Callers that already hold itimer_lock
 * (proc_timers_cancel) must NOT call this — they inline the clear instead.
 */
void proc_ptimers_clear(process_t *p) {
    uint32_t flags;

    if (!p) {
        return;
    }
    flags = intr_disable();
    spinlock_acquire(&p->itimer_lock);
    memset(p->ptimers, 0, sizeof(p->ptimers));
    p->n_ptimers = 0;
    p->n_ptimers_armed = 0;
    p->sig_timer_pend = 0;
    spinlock_release(&p->itimer_lock);
    intr_restore(flags);
}

void proc_timers_cancel(process_t *p) {
    uint32_t flags;

    if (!p) {
        return;
    }
    /* Disable IRQs so the timer ISR on the same CPU cannot try to acquire
     * itimer_lock while we hold it (the spinlock would detect same-CPU
     * re-entry and panic).  Same rationale for sys_setitimer/getitimer. */
    flags = intr_disable();
    spinlock_acquire(&p->itimer_lock);
    memset(p->itimer_value_ticks, 0, sizeof(p->itimer_value_ticks));
    memset(p->itimer_interval_ticks, 0, sizeof(p->itimer_interval_ticks));
    /* Inline the POSIX-timer clear rather than calling proc_ptimers_clear():
     * we already hold itimer_lock and it now takes the lock itself. */
    memset(p->ptimers, 0, sizeof(p->ptimers));
    p->n_ptimers = 0;
    p->n_ptimers_armed = 0;
    p->sig_timer_pend = 0;
    spinlock_release(&p->itimer_lock);
    intr_restore(flags);
}

void timer_init(void) {
    uint32_t divisor;

    /* Calibrate TSC against PIT channel 2 BEFORE we program channel 0 for
     * periodic ticks.  Doing it first keeps the calibration deterministic
     * (no IRQs touching channel 0 mid-poll). */
    tsc_calibrate();

    divisor = (PIT_FREQUENCY + (HZ / 2U)) / HZ;
    if (divisor == 0) {
        divisor = 1;
    }
    if (divisor > 0xFFFFU) {
        divisor = 0xFFFFU;
    }

    outb(PIT_COMMAND, PIT_MODE_RATE_GENERATOR);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFFU));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFFU));
}

uint64_t get_ticks(void) {
    return ticks;
}

time_t get_time(void) {
    return boot_time + (time_t)uptime_secs;   /* no 64-bit divide */
}

time_t get_boot_time(void) {
    return boot_time;
}

time_t get_uptime(void) {
    return (time_t)uptime_secs;               /* no 64-bit divide */
}

int64_t get_uptime_ms(void) {
    /* 1000 / HZ is exact for HZ that divides 1000 (e.g. 250 -> 4), so this is
     * a multiply, not a 64-bit divide.  Hammered in the USB poll spin. */
#if (1000 % HZ) == 0
    return (int64_t)(ticks * (uint64_t)(1000 / HZ));
#else
    return (int64_t)((ticks * 1000) / HZ);
#endif
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
    if (!t) return -EFAULT;
    boot_time = *t - (ticks / HZ);
    return 0;
}
int kern_gettimeofday(struct timeval *tv, struct timezone *tz) {
    if (!tv) return -EFAULT;

    time_t total_seconds = boot_time + (ticks / HZ);
    uint64_t base_usec = ((ticks % HZ) * 1000000ULL) / HZ;
    base_usec += subtick_nsec() / 1000ULL;
    if (base_usec >= 1000000ULL) {
        /* Sub-tick overflowed past a full second — unlikely (capped to
         * <tick worth of ns) but defensive. */
        total_seconds += (time_t)(base_usec / 1000000ULL);
        base_usec %= 1000000ULL;
    }

    tv->tv_sec = total_seconds;
    tv->tv_usec = (suseconds_t)base_usec;

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
    if (!tp) return -EFAULT;

    /*
     * CPU-time clocks (read-only).  CLOCK_PROCESS_CPUTIME_ID is the total
     * (user + system) CPU time consumed by the calling process, accumulated in
     * per-process rusage by rusage_add_tick() on every timer tick.
     */
    if (clk_id == CLOCK_PROCESS_CPUTIME_ID) {
        if (!current_process) return -EINVAL;
        uint32_t fl = intr_disable();
        struct timeval u = current_process->rusage.ru_utime;
        struct timeval s = current_process->rusage.ru_stime;
        intr_restore(fl);
        time_t sec = u.tv_sec + s.tv_sec;
        long   usec = u.tv_usec + s.tv_usec;
        if (usec >= 1000000) { sec += usec / 1000000; usec %= 1000000; }
        tp->tv_sec  = sec;
        tp->tv_nsec = usec * 1000;
        return 0;
    }

    /*
     * CLOCK_THREAD_CPUTIME_ID reports the CALLING THREAD's CPU time, charged
     * per timer tick to current_thread->utime/stime (HZ ticks) by
     * rusage_add_tick().  A freshly created thread's counters start at 0, so
     * its clock reads ~0 immediately after creation (pthread_create/11-1).
     */
    if (clk_id == CLOCK_THREAD_CPUTIME_ID) {
        if (!current_thread) return -EINVAL;
        uint32_t fl = intr_disable();
        uint32_t tticks = current_thread->cpu_utime + current_thread->cpu_stime;
        intr_restore(fl);
        uint64_t usec = (uint64_t)tticks * (uint64_t)USEC_PER_TICK;
        tp->tv_sec  = (time_t)(usec / 1000000ULL);
        tp->tv_nsec = (long)((usec % 1000000ULL) * 1000ULL);
        return 0;
    }

    time_t sec_base;
    if (clk_id == CLOCK_REALTIME) {
        sec_base = boot_time + (ticks / HZ);
    } else if (clk_id == CLOCK_MONOTONIC) {
        sec_base = (time_t)(ticks / HZ);
    } else {
        /* Unknown clock id: POSIX requires EINVAL (a bare -1 would become
         * EPERM in libc — see OPTS clock_gettime/8-1,8-2). */
        return -EINVAL;
    }

    uint64_t nsec = ((ticks % HZ) * 1000000000ULL) / HZ;
    nsec += subtick_nsec();
    if (nsec >= 1000000000ULL) {
        sec_base += (time_t)(nsec / 1000000000ULL);
        nsec %= 1000000000ULL;
    }

    tp->tv_sec  = sec_base;
    tp->tv_nsec = (long)nsec;
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
    uint32_t flags;

    if (!current_process || !curr_value || idx < 0) {
        return -EINVAL;
    }

    flags = intr_disable();
    spinlock_acquire(&current_process->itimer_lock);
    proc_getitimer_locked(current_process, idx, curr_value);
    spinlock_release(&current_process->itimer_lock);
    intr_restore(flags);
    return 0;
}

int kern_setitimer(int which, const struct itimerval *new_value, struct itimerval *old_value) {
    int idx = proc_itimer_index(which);
    uint32_t flags;

    if (!current_process || !new_value || idx < 0) {
        return -EINVAL;
    }
    if (!timeval_is_valid(&new_value->it_interval) || !timeval_is_valid(&new_value->it_value)) {
        return -EINVAL;
    }

    flags = intr_disable();
    spinlock_acquire(&current_process->itimer_lock);
    if (old_value) {
        proc_getitimer_locked(current_process, idx, old_value);
    }
    current_process->itimer_interval_ticks[idx] = timeval_to_ticks(&new_value->it_interval);
    current_process->itimer_value_ticks[idx] = timeval_to_ticks(&new_value->it_value);
    spinlock_release(&current_process->itimer_lock);
    intr_restore(flags);
    return 0;
}

/* ============================================================
 * POSIX.1b per-process interval timers (timer_create(2)).
 *
 * Each process owns a fixed table of timers (process_t.ptimers[]); the
 * timer_t handed to userspace is the slot index.  A timer's next expiry is
 * kept as an absolute nanosecond deadline in its clock domain
 * (CLOCK_REALTIME / CLOCK_MONOTONIC) and evaluated once per tick in
 * proc_ptimers_fire().  Because the deadline is stored in real nanoseconds
 * (not tick counts), the overrun count reflects true elapsed time even
 * though the kernel tick (HZ) is coarse.
 *
 * Overrun accounting follows POSIX: the first expiry of an idle timer
 * generates the signal; further expiries while that signal is still pending
 * accumulate in overrun_pending.  When the signal is accepted/delivered
 * (ptimer_signal_delivered) the accumulated value is latched into overrun
 * for timer_getoverrun(2).
 * ============================================================ */

#define NS_PER_SEC 1000000000ULL

static uint64_t timespec_to_ns(const struct timespec *ts) {
    if (ts->tv_sec < 0) {
        return 0;
    }
    uint64_t sec = (uint64_t)ts->tv_sec;
    if (sec > UINT64_MAX / NS_PER_SEC) {
        return UINT64_MAX;
    }
    uint64_t ns = sec * NS_PER_SEC;
    uint64_t frac = (ts->tv_nsec > 0) ? (uint64_t)ts->tv_nsec : 0;
    if (ns > UINT64_MAX - frac) {
        return UINT64_MAX;
    }
    return ns + frac;
}

static void ns_to_timespec(uint64_t ns, struct timespec *ts) {
    ts->tv_sec = (time_t)(ns / NS_PER_SEC);
    ts->tv_nsec = (long)(ns % NS_PER_SEC);
}

/* POSIX field validity for timer_settime: sec >= 0 and 0 <= nsec < 1e9. */
static int timespec_field_valid(const struct timespec *ts) {
    return ts->tv_sec >= 0 && ts->tv_nsec >= 0 && ts->tv_nsec < (long)NS_PER_SEC;
}

/* Absolute "now" in nanoseconds for the timer's clock domain. */
static uint64_t ptimer_now_ns(int clockid) {
    struct timespec ts;
    if (kern_clock_gettime((clockid_t)clockid, &ts) != 0) {
        return 0;
    }
    return timespec_to_ns(&ts);
}

/* Report the remaining time (it_value) and reload (it_interval) of timer t.
 * Caller holds itimer_lock. */
static void ptimer_gettime_locked(process_t *p, struct posix_timer *t,
                                  struct itimerspec *out) {
    (void)p;
    memset(out, 0, sizeof(*out));
    ns_to_timespec(t->interval_ns, &out->it_interval);
    if (t->armed) {
        uint64_t now = ptimer_now_ns(t->abs_real ? CLOCK_REALTIME : CLOCK_MONOTONIC);
        uint64_t rem = (t->next_ns > now) ? (t->next_ns - now) : 0;
        ns_to_timespec(rem, &out->it_value);
    }
    /* A disarmed timer reports it_value == {0,0} (POSIX). */
}

int kern_timer_create(int clockid, struct sigevent *ev, int *timerid) {
    if (!current_process || !timerid) {
        return -EINVAL;
    }
    /* Substrate backs CLOCK_REALTIME and CLOCK_MONOTONIC; the CPU-time
     * clocks and any bogus id are EINVAL (POSIX). */
    if (clockid != CLOCK_REALTIME && clockid != CLOCK_MONOTONIC) {
        return -EINVAL;
    }

    int notify = SIGEV_SIGNAL;
    int signo = SIGALRM;
    union sigval value;
    value.sival_int = 0;

    if (ev) {
        notify = ev->sigev_notify;
        if (notify == SIGEV_SIGNAL) {
            signo = ev->sigev_signo;
            value = ev->sigev_value;
            /* Valid signal numbers are 1..NSIG-1 (sigmask/sigprop are sized
             * NSIG); reject signo == NSIG so a timer can't target a bit that
             * psignal()/sigprop[] treat as out of range. */
            if (signo < 1 || signo >= NSIG) {
                return -EINVAL;
            }
        } else if (notify == SIGEV_NONE) {
            signo = 0;
        } else {
            /* SIGEV_THREAD is realized by libc, never by the kernel. */
            return -EINVAL;
        }
    }

    uint32_t fl = intr_disable();
    spinlock_acquire(&current_process->itimer_lock);
    int id = -1;
    for (int i = 0; i < POSIX_TIMER_MAX; i++) {
        if (!current_process->ptimers[i].used) {
            id = i;
            break;
        }
    }
    if (id < 0) {
        spinlock_release(&current_process->itimer_lock);
        intr_restore(fl);
        return -EAGAIN;   /* per-process timer table full */
    }
    struct posix_timer *t = &current_process->ptimers[id];
    memset(t, 0, sizeof(*t));
    t->used = 1;
    t->clockid = clockid;
    t->notify = (uint8_t)notify;
    t->signo = signo;
    t->value = value;
    /* POSIX: a NULL sigevent defaults sigev_value to the timer id. */
    if (!ev) {
        t->value.sival_int = id;
    }
    current_process->n_ptimers++;
    spinlock_release(&current_process->itimer_lock);
    intr_restore(fl);

    *timerid = id;
    return 0;
}

int kern_timer_settime(int id, int flags, const struct itimerspec *nv,
                       struct itimerspec *ov) {
    if (!current_process) {
        return -EINVAL;
    }
    if (id < 0 || id >= POSIX_TIMER_MAX || !nv) {
        return -EINVAL;
    }
    if (!timespec_field_valid(&nv->it_value) ||
        !timespec_field_valid(&nv->it_interval)) {
        return -EINVAL;
    }

    uint32_t fl = intr_disable();
    spinlock_acquire(&current_process->itimer_lock);
    struct posix_timer *t = &current_process->ptimers[id];
    if (!t->used) {
        spinlock_release(&current_process->itimer_lock);
        intr_restore(fl);
        return -EINVAL;
    }

    if (ov) {
        ptimer_gettime_locked(current_process, t, ov);
    }

    uint64_t value_ns = timespec_to_ns(&nv->it_value);
    uint64_t interval_ns = timespec_to_ns(&nv->it_interval);
    int was_armed = t->armed;

    if (value_ns == 0) {
        /* it_value == 0 disarms the timer (POSIX). */
        t->armed = 0;
        t->next_ns = 0;
        t->interval_ns = 0;
        t->abs_real = 0;
        if (was_armed && current_process->n_ptimers_armed > 0) {
            current_process->n_ptimers_armed--;
        }
    } else {
        /*
         * Only an ABSOLUTE CLOCK_REALTIME timer tracks the wall clock and so
         * must follow a clock_settime(2) step (OPTS clock_settime/4-1).  A
         * RELATIVE arming — and every CLOCK_MONOTONIC timer — measures an
         * interval and must be immune to clock steps (OPTS clock_settime/5-1),
         * so anchor it to the monotonic base.  next_ns is thereafter always in
         * the domain named by t->abs_real.
         */
        int abs_real = (t->clockid == CLOCK_REALTIME) && (flags & TIMER_ABSTIME);
        uint64_t now = ptimer_now_ns(abs_real ? CLOCK_REALTIME : CLOCK_MONOTONIC);
        if (flags & TIMER_ABSTIME) {
            /* Absolute deadline in the timer's own clock domain; a time
             * already in the past fires ASAP. */
            t->next_ns = (value_ns > now) ? value_ns : now;
        } else {
            t->next_ns = (now > UINT64_MAX - value_ns) ? UINT64_MAX
                                                       : now + value_ns;
        }
        t->interval_ns = interval_ns;
        t->abs_real = (uint8_t)abs_real;
        if (!was_armed) {
            current_process->n_ptimers_armed++;
        }
        t->armed = 1;
    }

    /* Re-arming (or disarming) resets the timer: drop any stale overrun
     * accounting and pending SI_TIMER signal for a clean next expiry. */
    t->overrun = 0;
    t->overrun_pending = 0;
    t->sig_outstanding = 0;
    if (t->notify == SIGEV_SIGNAL && t->signo >= 1 && t->signo <= NSIG) {
        current_process->sig_timer_pend &= ~sigmask(t->signo);
    }

    spinlock_release(&current_process->itimer_lock);
    intr_restore(fl);
    return 0;
}

int kern_timer_gettime(int id, struct itimerspec *curr) {
    if (!current_process || !curr) {
        return -EINVAL;
    }
    if (id < 0 || id >= POSIX_TIMER_MAX) {
        return -EINVAL;
    }
    uint32_t fl = intr_disable();
    spinlock_acquire(&current_process->itimer_lock);
    struct posix_timer *t = &current_process->ptimers[id];
    if (!t->used) {
        spinlock_release(&current_process->itimer_lock);
        intr_restore(fl);
        return -EINVAL;
    }
    ptimer_gettime_locked(current_process, t, curr);
    spinlock_release(&current_process->itimer_lock);
    intr_restore(fl);
    return 0;
}

int kern_timer_delete(int id) {
    if (!current_process) {
        return -EINVAL;
    }
    if (id < 0 || id >= POSIX_TIMER_MAX) {
        return -EINVAL;
    }
    uint32_t fl = intr_disable();
    spinlock_acquire(&current_process->itimer_lock);
    struct posix_timer *t = &current_process->ptimers[id];
    if (!t->used) {
        spinlock_release(&current_process->itimer_lock);
        intr_restore(fl);
        return -EINVAL;
    }
    if (t->armed && current_process->n_ptimers_armed > 0) {
        current_process->n_ptimers_armed--;
    }
    if (current_process->n_ptimers > 0) {
        current_process->n_ptimers--;
    }
    if (t->notify == SIGEV_SIGNAL && t->signo >= 1 && t->signo <= NSIG) {
        current_process->sig_timer_pend &= ~sigmask(t->signo);
    }
    memset(t, 0, sizeof(*t));
    spinlock_release(&current_process->itimer_lock);
    intr_restore(fl);
    return 0;
}

int kern_timer_getoverrun(int id) {
    if (!current_process) {
        return -EINVAL;
    }
    if (id < 0 || id >= POSIX_TIMER_MAX) {
        return -EINVAL;
    }
    uint32_t fl = intr_disable();
    spinlock_acquire(&current_process->itimer_lock);
    struct posix_timer *t = &current_process->ptimers[id];
    if (!t->used) {
        spinlock_release(&current_process->itimer_lock);
        intr_restore(fl);
        return -EINVAL;
    }
    int ov = t->overrun;
    spinlock_release(&current_process->itimer_lock);
    intr_restore(fl);
    return ov;
}

/*
 * proc_ptimers_fire - Evaluate a process's POSIX timers against the tick.
 *
 * Called from the timer ISR (CPU 0) for every non-kernel process.  Uses
 * spinlock_try_acquire like the itimer path: a syscall on another CPU may
 * hold itimer_lock, in which case this tick is skipped (the absolute
 * deadline means no expiry is lost — the next tick picks it up).  psignal()
 * is deferred until after the lock is dropped.
 */
void proc_ptimers_fire(process_t *p) {
    if (!p || p->n_ptimers_armed == 0) {
        return;
    }
    if (p->state == SDYING || p->state == SZOMB) {
        return;
    }
    if (!spinlock_try_acquire(&p->itimer_lock)) {
        return;
    }

    int fire[POSIX_TIMER_MAX];
    int nfire = 0;

    for (int i = 0; i < POSIX_TIMER_MAX; i++) {
        struct posix_timer *t = &p->ptimers[i];
        if (!t->used || !t->armed) {
            continue;
        }
        uint64_t now = ptimer_now_ns(t->abs_real ? CLOCK_REALTIME : CLOCK_MONOTONIC);
        if (now < t->next_ns) {
            continue;
        }

        /* How many expirations have actually occurred by now (>= 1). */
        uint64_t nexp;
        if (t->interval_ns == 0) {
            nexp = 1;
            t->armed = 0;
            t->next_ns = 0;
            if (p->n_ptimers_armed > 0) {
                p->n_ptimers_armed--;
            }
        } else {
            nexp = (now - t->next_ns) / t->interval_ns + 1;
            t->next_ns += nexp * t->interval_ns;
        }

        if (!t->sig_outstanding) {
            /* Generate the notification; extra expirations are overruns. */
            t->sig_outstanding = 1;
            t->overrun_pending = (int)(nexp - 1);
            if (t->notify == SIGEV_SIGNAL && t->signo >= 1 && t->signo <= NSIG) {
                p->sig_qval[t->signo - 1] = t->value;
                __sync_fetch_and_or(&p->sig_timer_pend, sigmask(t->signo));
                if (nfire < POSIX_TIMER_MAX) {
                    fire[nfire++] = t->signo;
                }
            }
        } else {
            /* The previous notification is still pending: all of these
             * expirations count as overruns. */
            t->overrun_pending += (int)nexp;
        }
    }
    spinlock_release(&p->itimer_lock);

    for (int i = 0; i < nfire; i++) {
        psignal(p, fire[i]);
    }
}

/*
 * ptimer_signal_delivered - Latch timer overrun at signal acceptance.
 *
 * Called from signal_handle_pending() when a signal is dequeued for
 * delivery/acceptance.  For each timer of this process whose outstanding
 * notification uses this signal, the accumulated overrun_pending is latched
 * into overrun (visible to timer_getoverrun) and the generation state is
 * reset so the next expiry starts a fresh count.
 */
void ptimer_signal_delivered(process_t *p, int sig) {
    if (!p || sig < 1 || sig >= NSIG) {
        return;
    }
    if (p->n_ptimers == 0) {
        return;
    }
    uint32_t fl = intr_disable();
    spinlock_acquire(&p->itimer_lock);
    for (int i = 0; i < POSIX_TIMER_MAX; i++) {
        struct posix_timer *t = &p->ptimers[i];
        if (!t->used || t->notify != SIGEV_SIGNAL || t->signo != sig) {
            continue;
        }
        if (!t->sig_outstanding) {
            continue;
        }
        t->overrun = t->overrun_pending;
        t->overrun_pending = 0;
        t->sig_outstanding = 0;
    }
    /* When the signal is not caught (SIG_DFL/SIG_IGN) no siginfo is built,
     * so drop the SI_TIMER marker now to keep a stale si_value from bleeding
     * into a later, unrelated instance of the same signal. */
    {
        sig_t h = p->sig_actions[sig - 1].sa_handler;
        if (h == SIG_DFL || h == SIG_IGN) {
            p->sig_timer_pend &= ~sigmask(sig);
        }
    }
    spinlock_release(&p->itimer_lock);
    intr_restore(fl);
}

int sys_timer_create(int clockid, struct sigevent *sevp, int *timerid) {
    struct sigevent kev;
    struct sigevent *evp = NULL;
    int kid = -1;

    if (!timerid) {
        return -EFAULT;
    }
    if (sevp) {
        if (copyin(sevp, &kev, sizeof(kev)) != 0) {
            return -EFAULT;
        }
        evp = &kev;
    }
    int r = kern_timer_create(clockid, evp, &kid);
    if (r != 0) {
        return r;
    }
    if (copyout(&kid, timerid, sizeof(int)) != 0) {
        return -EFAULT;
    }
    return 0;
}

int sys_timer_settime(int id, int flags, const struct itimerspec *nv,
                      struct itimerspec *ov) {
    struct itimerspec knv, kov;

    if (!nv) {
        return -EINVAL;
    }
    if (copyin(nv, &knv, sizeof(knv)) != 0) {
        return -EFAULT;
    }
    int r = kern_timer_settime(id, flags, &knv, ov ? &kov : NULL);
    if (r == 0 && ov && copyout(&kov, ov, sizeof(kov)) != 0) {
        return -EFAULT;
    }
    return r;
}

int sys_timer_gettime(int id, struct itimerspec *curr) {
    struct itimerspec k;

    if (!curr) {
        return -EFAULT;
    }
    int r = kern_timer_gettime(id, &k);
    if (r == 0 && copyout(&k, curr, sizeof(k)) != 0) {
        return -EFAULT;
    }
    return r;
}

int sys_timer_delete(int id) {
    return kern_timer_delete(id);
}

int sys_timer_getoverrun(int id) {
    return kern_timer_getoverrun(id);
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
    /* Setting the wall clock is privileged — otherwise any process could
     * jump the system time (breaking timers, make, TLS validity, at/cron). */
    if (current_process && current_process->euid != 0) return -EPERM;
    if (copyin(t, &kt, sizeof(time_t)) != 0) return -EFAULT;
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
    int cpu_id = percpu_get_cpu_id();

    /*
     * Global wall-clock and timeout accounting must advance exactly once per
     * system tick. On SMP, other CPUs may have local timer sources for
     * preemption, but they must not multiply global timekeeping.
     */
    if (cpu_id == 0) {
        ticks++;
        /* Maintain wall-clock seconds incrementally (avoids a per-call
         * 64-bit divide in get_time/get_uptime — see uptime_secs above). */
        if (++tick_in_sec >= HZ) { tick_in_sec = 0; uptime_secs++; }
        if (tsc_hz != 0) tsc_at_last_tick = i386_cpu_cycle_counter();
        sched_tick();
        hw_text_tick();
        fb_console_tick();
        floppy_poll();
        if ((ticks % (5 * HZ)) == 0) {
            sched_update_loadavg();
        }
        if ((ticks % HZ) == 0) {
            vt_tick_1hz();
        }
        FOREACH_PROC(p) {
            if (p->is_kernel_task) {
                continue;
            }
            proc_timer_fire(p, ITIMER_REAL);
            proc_ptimers_fire(p);
        }
    }

    if (current_process && current_process->pid != -1 && !current_process->is_kernel_task) {
        /* CPU accounting: charge this tick to user or system time of
         * the running process.  utime/stime are uint32_t in HZ ticks
         * (POSIX SC_CLK_TCK semantics).  Reads via sys_proc_info,
         * sys_times, getrusage, and procfs all consume these fields.
         * Without this, ps' TIME column was always 00:00:00. */
        if (is_usermode) {
            current_process->utime++;
            proc_timer_fire(current_process, ITIMER_VIRTUAL);
        } else {
            current_process->stime++;
        }
        proc_timer_fire(current_process, ITIMER_PROF);
    }
}

void timer_tick(void) {
    timer_tick_context(0);
}
