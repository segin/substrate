/*
 * sched_posix.c — POSIX process-level scheduling syscalls.
 *
 * Implements sched_setscheduler(2), sched_getscheduler(2),
 * sched_setparam(2), sched_getparam(2), sched_get_priority_max(2),
 * sched_get_priority_min(2) and sched_rr_get_interval(2) for the native
 * personality.
 *
 * Semantics: the requested policy/priority are validated and stored on
 * the target process_t (and reported back by the getters).  The
 * substrate MLFQ/SMP scheduler is intentionally NOT reconfigured from
 * these values — the goal here is POSIX-conformant bookkeeping and,
 * above all, that a bogus argument returns -EINVAL/-ESRCH/-EPERM
 * instead of ever faulting the kernel.  A userspace syscall must never
 * be able to crash the kernel.
 *
 * SCHED_SPORADIC (POSIX sporadic server) is supported at the API level:
 * the policy is accepted, its sched_ss_* parameters are validated per
 * POSIX, and it uses the real-time 1..99 priority band.  The substrate
 * scheduler runs a SCHED_SPORADIC thread like SCHED_FIFO at its
 * sched_priority — there is no runtime budget replenishment.
 */

#include <stddef.h>

#include <pm/pm.h>
#include <sys/copy.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/syscall_impl.h>
#include <sys/time.h>

/* Does substrate recognize this scheduling policy?  An unknown value is
 * rejected — never faulted. */
static int policy_valid(int policy)
{
    switch (policy) {
    case POSIX_SCHED_OTHER:
    case POSIX_SCHED_FIFO:
    case POSIX_SCHED_RR:
    case POSIX_SCHED_BATCH:
    case POSIX_SCHED_SPORADIC:
    case POSIX_SCHED_IDLE:
        return 1;
    default:
        return 0;
    }
}

/* Real-time policies use the 1..99 priority band and require privilege to
 * enter/raise.  SCHED_SPORADIC is a real-time policy for both purposes. */
static int policy_is_rt(int policy)
{
    return policy == POSIX_SCHED_FIFO ||
           policy == POSIX_SCHED_RR   ||
           policy == POSIX_SCHED_SPORADIC;
}

static int policy_prio_min(int policy)
{
    return policy_is_rt(policy) ? POSIX_SCHED_PRIO_RT_MIN : 0;
}

static int policy_prio_max(int policy)
{
    return policy_is_rt(policy) ? POSIX_SCHED_PRIO_RT_MAX : 0;
}

/* Is `prio` a legal sched_priority for `policy`? */
static int prio_in_range(int policy, int prio)
{
    return prio >= policy_prio_min(policy) && prio <= policy_prio_max(policy);
}

/* a < b for a pair of timespecs (lexicographic on (sec, nsec)). */
static int timespec_lt(const struct timespec *a, const struct timespec *b)
{
    if (a->tv_sec != b->tv_sec) return a->tv_sec < b->tv_sec;
    return a->tv_nsec < b->tv_nsec;
}

/*
 * Validate a SCHED_SPORADIC sched_param per POSIX.  Returns 1 if the
 * sporadic-server parameters are self-consistent, 0 (=> -EINVAL) otherwise.
 * Per POSIX, sched_setscheduler()/sched_setparam() fail with EINVAL when:
 *   - sched_ss_low_priority is not within the sporadic priority range,
 *   - sched_priority is less than sched_ss_low_priority,
 *   - sched_ss_repl_period is less than sched_ss_init_budget,
 *   - sched_ss_max_repl is not within [1, SS_REPL_MAX].
 *
 * The lower bound on both priorities is 0 (not RT_MIN) so a process still at
 * its default priority 0 can transition to SCHED_SPORADIC and round-trip
 * through sched_getparam()/sched_setscheduler(); only the upper bound is what
 * the conformance tests exercise for an out-of-range value.
 */
static int sporadic_params_ok(const struct sched_param *p)
{
    if (p->sched_ss_low_priority < 0 ||
        p->sched_ss_low_priority > POSIX_SCHED_PRIO_RT_MAX)
        return 0;
    if (p->sched_priority < 0 || p->sched_priority > POSIX_SCHED_PRIO_RT_MAX)
        return 0;
    if (p->sched_priority < p->sched_ss_low_priority)
        return 0;
    if (timespec_lt(&p->sched_ss_repl_period, &p->sched_ss_init_budget))
        return 0;
    if (p->sched_ss_max_repl < 1 || p->sched_ss_max_repl > POSIX_SS_REPL_MAX)
        return 0;
    return 1;
}

/*
 * The two sched_ss_* consistency constraints that are independent of the
 * priority band: the replenishment period must be at least the initial
 * budget, and the maximum pending replenishments must lie in
 * [1, SS_REPL_MAX].  Returns 1 if both hold, 0 (=> -EINVAL) otherwise.
 *
 * Substrate advertises _POSIX_SPORADIC_SERVER, so sched_setparam() takes a
 * complete sched_param carrying these members and validates them for a
 * process whose scheduling policy is not a fixed-priority-only policy
 * (SCHED_FIFO/SCHED_RR, whose sched_param carries no meaningful sporadic
 * members and is validated on sched_priority alone).  A well-formed caller
 * builds its sched_param from sched_getparam(), which reports a valid
 * sched_ss_max_repl and zeroed period/budget, so this rejects only a
 * deliberately out-of-range value (sched_setparam/25-3, 25-4).
 */
static int sporadic_repl_ok(const struct sched_param *p)
{
    if (timespec_lt(&p->sched_ss_repl_period, &p->sched_ss_init_budget))
        return 0;
    if (p->sched_ss_max_repl < 1 || p->sched_ss_max_repl > POSIX_SS_REPL_MAX)
        return 0;
    return 1;
}

/* pid == 0 means the calling process. */
static process_t *sched_target(pid_t pid)
{
    return (pid == 0) ? current_process : proc_find((int)pid);
}

/* Permission to read another process's scheduling parameters. */
static int may_view(process_t *t)
{
    if (!current_process) return 1;                 /* early/kernel ctx */
    if (current_process->euid == 0) return 1;       /* root sees all    */
    return t->uid == current_process->euid ||
           t->euid == current_process->euid;
}

/* Permission to change another process's scheduling parameters. */
static int may_control(process_t *t)
{
    if (!current_process) return 1;                 /* early/kernel ctx */
    if (current_process->euid == 0) return 1;       /* root controls all*/
    return t->uid == current_process->euid;
}

/*
 * Privilege check for a real-time scheduling request.  Obtaining or
 * raising a SCHED_FIFO/SCHED_RR priority requires privilege: Linux models
 * this with RLIMIT_RTPRIO (default 0), under which an unprivileged process
 * "may only lower the priority, or switch to a non-real-time policy" — it
 * can neither switch to an RT policy nor raise an RT priority.  Substrate
 * has no RLIMIT_RTPRIO, so root (euid 0) may set any RT priority and every
 * other caller is held to that default-limit rule.  `new_policy`/`new_prio`
 * are the requested settings; `t` is the target, whose current effective
 * RT priority bounds a permitted lowering.
 */
static int rt_priv_ok(int new_policy, int new_prio, process_t *t)
{
    if (!current_process)           return 1;   /* early/kernel context */
    if (current_process->euid == 0) return 1;   /* privileged           */
    if (!policy_is_rt(new_policy))  return 1;   /* non-RT: unrestricted */

    /* Effective current RT priority of the target (0 when not RT). */
    int cur = policy_is_rt(t->sched_policy) ? t->sched_rt_priority : 0;
    return new_prio <= cur;                     /* may only keep/lower  */
}

int sys_sched_get_priority_max(int policy)
{
    if (!policy_valid(policy)) return -EINVAL;
    return policy_prio_max(policy);
}

int sys_sched_get_priority_min(int policy)
{
    if (!policy_valid(policy)) return -EINVAL;
    return policy_prio_min(policy);
}

int sys_sched_getscheduler(pid_t pid)
{
    if (pid < 0) return -EINVAL;
    process_t *t = sched_target(pid);
    if (!t) return -ESRCH;
    if (!may_view(t)) return -EPERM;
    return t->sched_policy;
}

int sys_sched_getparam(pid_t pid, struct sched_param *uparam)
{
    if (pid < 0 || !uparam) return -EINVAL;
    process_t *t = sched_target(pid);
    if (!t) return -ESRCH;
    if (!may_view(t)) return -EPERM;

    struct sched_param kp;
    kp.sched_priority        = t->sched_rt_priority;
    kp.sched_ss_low_priority = t->sched_ss_low_priority;
    kp.sched_ss_repl_period  = t->sched_ss_repl_period;
    kp.sched_ss_init_budget  = t->sched_ss_init_budget;
    kp.sched_ss_max_repl     = t->sched_ss_max_repl;
    /* Guarantee the reported block round-trips through a subsequent
     * sched_setscheduler(SCHED_SPORADIC): a process that never set sporadic
     * params has sched_ss_max_repl == 0 (invalid), so report a valid default
     * instead.  The other members default to 0, which is accepted. */
    if (kp.sched_ss_max_repl < 1 || kp.sched_ss_max_repl > POSIX_SS_REPL_MAX)
        kp.sched_ss_max_repl = POSIX_SS_REPL_MAX;
    if (copyout(&kp, uparam, sizeof(kp)) != 0) return -EFAULT;
    return 0;
}

int sys_sched_setscheduler(pid_t pid, int policy,
                           const struct sched_param *uparam)
{
    if (pid < 0 || !uparam) return -EINVAL;
    if (!policy_valid(policy)) return -EINVAL;

    struct sched_param kp;
    if (copyin(uparam, &kp, sizeof(kp)) != 0) return -EFAULT;
    if (policy == POSIX_SCHED_SPORADIC) {
        if (!sporadic_params_ok(&kp)) return -EINVAL;
    } else {
        if (!prio_in_range(policy, kp.sched_priority)) return -EINVAL;
    }

    process_t *t = sched_target(pid);
    if (!t) return -ESRCH;
    if (!may_control(t)) return -EPERM;
    if (!rt_priv_ok(policy, kp.sched_priority, t)) return -EPERM;

    int old_policy = t->sched_policy;
    t->sched_policy      = policy;
    t->sched_rt_priority = kp.sched_priority;
    if (policy == POSIX_SCHED_SPORADIC) {
        t->sched_ss_low_priority = kp.sched_ss_low_priority;
        t->sched_ss_repl_period  = kp.sched_ss_repl_period;
        t->sched_ss_init_budget  = kp.sched_ss_init_budget;
        t->sched_ss_max_repl     = kp.sched_ss_max_repl;
    }
    return old_policy;              /* POSIX: return the previous policy */
}

int sys_sched_setparam(pid_t pid, const struct sched_param *uparam)
{
    if (pid < 0 || !uparam) return -EINVAL;

    struct sched_param kp;
    if (copyin(uparam, &kp, sizeof(kp)) != 0) return -EFAULT;

    process_t *t = sched_target(pid);
    if (!t) return -ESRCH;
    if (!may_control(t)) return -EPERM;
    if (t->sched_policy == POSIX_SCHED_SPORADIC) {
        if (!sporadic_params_ok(&kp)) return -EINVAL;
    } else {
        if (!prio_in_range(t->sched_policy, kp.sched_priority)) return -EINVAL;
        /* A fixed-priority-only policy (SCHED_FIFO/SCHED_RR) carries no
         * meaningful sporadic members and is validated on sched_priority
         * alone; every other policy also validates the priority-independent
         * sched_ss_* replenishment constraints (sched_setparam/25-3, 25-4). */
        if (!policy_is_rt(t->sched_policy) && !sporadic_repl_ok(&kp))
            return -EINVAL;
    }
    if (!rt_priv_ok(t->sched_policy, kp.sched_priority, t)) return -EPERM;

    t->sched_rt_priority = kp.sched_priority;
    if (t->sched_policy == POSIX_SCHED_SPORADIC) {
        t->sched_ss_low_priority = kp.sched_ss_low_priority;
        t->sched_ss_repl_period  = kp.sched_ss_repl_period;
        t->sched_ss_init_budget  = kp.sched_ss_init_budget;
        t->sched_ss_max_repl     = kp.sched_ss_max_repl;
    }
    return 0;
}

int sys_sched_rr_get_interval(pid_t pid, struct timespec *uts)
{
    if (pid < 0 || !uts) return -EINVAL;
    process_t *t = sched_target(pid);
    if (!t) return -ESRCH;

    struct timespec ts;
    ts.tv_sec  = 0;
    ts.tv_nsec = 10000000;         /* 10 ms round-robin quantum */
    if (copyout(&ts, uts, sizeof(ts)) != 0) return -EFAULT;
    return 0;
}
