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
 * SCHED_SPORADIC is not supported: it is simply an unrecognized policy
 * and every entry point rejects it cleanly with -EINVAL.
 */

#include <include/sys/proc.h>
#include <pm/pm.h>
#include <sys/copy.h>
#include <sys/errno.h>
#include <sys/sched.h>
#include <sys/syscall_impl.h>
#include <sys/time.h>
#include <stddef.h>

/* Does substrate recognize this scheduling policy?  SCHED_SPORADIC and
 * any other unknown value are rejected — never faulted. */
static int policy_valid(int policy)
{
    switch (policy) {
    case POSIX_SCHED_OTHER:
    case POSIX_SCHED_FIFO:
    case POSIX_SCHED_RR:
    case POSIX_SCHED_BATCH:
    case POSIX_SCHED_IDLE:
        return 1;
    default:
        return 0;
    }
}

static int policy_is_rt(int policy)
{
    return policy == POSIX_SCHED_FIFO || policy == POSIX_SCHED_RR;
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
    kp.sched_priority = t->sched_rt_priority;
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
    if (!prio_in_range(policy, kp.sched_priority)) return -EINVAL;

    process_t *t = sched_target(pid);
    if (!t) return -ESRCH;
    if (!may_control(t)) return -EPERM;
    if (!rt_priv_ok(policy, kp.sched_priority, t)) return -EPERM;

    int old_policy = t->sched_policy;
    t->sched_policy      = policy;
    t->sched_rt_priority = kp.sched_priority;
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
    if (!prio_in_range(t->sched_policy, kp.sched_priority)) return -EINVAL;
    if (!rt_priv_ok(t->sched_policy, kp.sched_priority, t)) return -EPERM;

    t->sched_rt_priority = kp.sched_priority;
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
