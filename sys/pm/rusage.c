/*
 * rusage.c - Resource Usage Tracking and Calculation
 *
 * Implements proper tracking of user and system time for processes,
 * following BSD semantics. This is called from the timer interrupt
 * to accumulate time, and from proc_exit() to finalize the stats.
 *
 * Reference: FreeBSD's kern_resource.c, POSIX getrusage(2)
 */

#include <sys/proc.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/param.h>
#include <string.h>

/*
 * rusage_add_tick - Add one timer tick to rusage
 *
 * Called from the timer interrupt handler on each tick.
 * Determines whether the process was in user mode or kernel mode
 * based on the CS register and updates the appropriate counter.
 *
 * @p: The process to update
 * @is_usermode: True if the tick occurred while in user mode
 *
 * Note: This function is called with interrupts disabled.
 */
void rusage_add_tick(process_t *p, int is_usermode)
{
    struct timeval *tv;

    if (!p)
        return;

    if (is_usermode) {
        tv = &p->rusage.ru_utime;
    } else {
        tv = &p->rusage.ru_stime;
    }

    /* Add one tick worth of microseconds */
    tv->tv_usec += USEC_PER_TICK;

    /* Normalize: carry over to seconds if >= 1000000 usec */
    while (tv->tv_usec >= 1000000) {
        tv->tv_sec++;
        tv->tv_usec -= 1000000;
    }
}

/*
 * rusage_add_fault - Record a page fault event
 *
 * @p: The process to update
 * @major: True if this was a major fault (required I/O), false for minor
 */
void rusage_add_fault(process_t *p, int major)
{
    if (!p)
        return;

    if (major) {
        p->rusage.ru_majflt++;
    } else {
        p->rusage.ru_minflt++;
    }
}

/*
 * rusage_add_ctx_switch - Record a context switch event
 *
 * @p: The process to update
 * @voluntary: True if this was a voluntary context switch (sleep/wait)
 */
void rusage_add_ctx_switch(process_t *p, int voluntary)
{
    if (!p)
        return;

    if (voluntary) {
        p->rusage.ru_nvcsw++;
    } else {
        p->rusage.ru_nivcsw++;
    }
}

/*
 * rusage_add_signal - Record a signal received event
 *
 * @p: The process to update
 */
void rusage_add_signal(process_t *p)
{
    if (!p)
        return;

    p->rusage.ru_nsignals++;
}

/*
 * rusage_update_maxrss - Update maximum resident set size
 *
 * Should be called when memory is allocated to a process.
 *
 * @p: The process to update
 * @rss_pages: Current resident set size in pages
 */
void rusage_update_maxrss(process_t *p, long rss_pages)
{
    if (!p)
        return;

    /* Convert pages to kilobytes (4KB pages) */
    long rss_kb = rss_pages * 4;

    if (rss_kb > p->rusage.ru_maxrss) {
        p->rusage.ru_maxrss = rss_kb;
    }
}

/*
 * rusage_add_io - Record a block I/O operation
 *
 * @p: The process to update
 * @is_read: True if this was a read operation, false for write
 */
void rusage_add_io(process_t *p, int is_read)
{
    if (!p)
        return;

    if (is_read) {
        p->rusage.ru_inblock++;
    } else {
        p->rusage.ru_oublock++;
    }
}

/*
 * rusage_finalize - Calculate final rusage for a dying process
 *
 * Called from proc_exit() before transitioning to SZOMB state.
 * This function ensures all accumulated rusage data is properly
 * normalized and ready to be returned by wait4().
 *
 * @p: The process that is exiting
 */
void rusage_finalize(process_t *p)
{
    if (!p)
        return;

    /*
     * Normalize timeval structures to ensure tv_usec < 1000000.
     * This should already be the case from rusage_add_tick(),
     * but we do it here for safety.
     */
    while (p->rusage.ru_utime.tv_usec >= 1000000) {
        p->rusage.ru_utime.tv_sec++;
        p->rusage.ru_utime.tv_usec -= 1000000;
    }

    while (p->rusage.ru_stime.tv_usec >= 1000000) {
        p->rusage.ru_stime.tv_sec++;
        p->rusage.ru_stime.tv_usec -= 1000000;
    }

    /*
     * Add children's accumulated rusage to our own.
     * This way, when wait4() returns our rusage to our parent,
     * it includes all the resources consumed by our descendants.
     */
    p->rusage.ru_utime.tv_sec += p->rusage_children.ru_utime.tv_sec;
    p->rusage.ru_utime.tv_usec += p->rusage_children.ru_utime.tv_usec;
    while (p->rusage.ru_utime.tv_usec >= 1000000) {
        p->rusage.ru_utime.tv_sec++;
        p->rusage.ru_utime.tv_usec -= 1000000;
    }

    p->rusage.ru_stime.tv_sec += p->rusage_children.ru_stime.tv_sec;
    p->rusage.ru_stime.tv_usec += p->rusage_children.ru_stime.tv_usec;
    while (p->rusage.ru_stime.tv_usec >= 1000000) {
        p->rusage.ru_stime.tv_sec++;
        p->rusage.ru_stime.tv_usec -= 1000000;
    }

    /* Take the max of our maxrss and children's maxrss */
    if (p->rusage_children.ru_maxrss > p->rusage.ru_maxrss) {
        p->rusage.ru_maxrss = p->rusage_children.ru_maxrss;
    }

    /* Accumulate other stats from children */
    p->rusage.ru_minflt += p->rusage_children.ru_minflt;
    p->rusage.ru_majflt += p->rusage_children.ru_majflt;
    p->rusage.ru_nswap += p->rusage_children.ru_nswap;
    p->rusage.ru_inblock += p->rusage_children.ru_inblock;
    p->rusage.ru_oublock += p->rusage_children.ru_oublock;
    p->rusage.ru_msgsnd += p->rusage_children.ru_msgsnd;
    p->rusage.ru_msgrcv += p->rusage_children.ru_msgrcv;
    p->rusage.ru_nsignals += p->rusage_children.ru_nsignals;
    p->rusage.ru_nvcsw += p->rusage_children.ru_nvcsw;
    p->rusage.ru_nivcsw += p->rusage_children.ru_nivcsw;
}

/*
 * rusage_init - Initialize rusage structures for a new process
 *
 * Called when creating a new process to zero out the rusage fields.
 *
 * @p: The newly created process
 */
void rusage_init(process_t *p)
{
    if (!p)
        return;

    memset(&p->rusage, 0, sizeof(struct rusage));
    memset(&p->rusage_children, 0, sizeof(struct rusage));
}

/*
 * rusage_copy_to_child - Copy rusage stats during fork
 *
 * Child starts with zeroed rusage. Parent's rusage is not modified.
 *
 * @child: The newly forked child process
 */
void rusage_copy_to_child(process_t *child)
{
    if (!child)
        return;

    /* Child starts fresh - no accumulated time or stats */
    memset(&child->rusage, 0, sizeof(struct rusage));
    memset(&child->rusage_children, 0, sizeof(struct rusage));
}

/*
 * timeval_add - Add two timevals together
 *
 * Helper function for rusage calculations.
 *
 * @result: Output timeval (may be same as a or b)
 * @a: First timeval
 * @b: Second timeval
 */
void timeval_add(struct timeval *result, const struct timeval *a,
                 const struct timeval *b)
{
    result->tv_sec = a->tv_sec + b->tv_sec;
    result->tv_usec = a->tv_usec + b->tv_usec;

    while (result->tv_usec >= 1000000) {
        result->tv_sec++;
        result->tv_usec -= 1000000;
    }
}

/*
 * sys_getrusage - Get resource usage (getrusage syscall)
 *
 * @who: RUSAGE_SELF for process, RUSAGE_CHILDREN for children,
 *       RUSAGE_THREAD for current thread (Linux extension)
 * @usage: Pointer to struct rusage to fill in
 *
 * Returns: 0 on success, -EINVAL on invalid who value
 */
int sys_getrusage(int who, struct rusage *usage)
{
    extern process_t *current_process;

    if (!usage || !current_process)
        return -22; /* EINVAL */

    switch (who) {
    case RUSAGE_SELF:
        *usage = current_process->rusage;
        break;

    case RUSAGE_CHILDREN:
        *usage = current_process->rusage_children;
        break;

    case RUSAGE_THREAD:
        /* For now, thread rusage is same as process rusage */
        *usage = current_process->rusage;
        break;

    default:
        return -22; /* EINVAL */
    }

    return 0;
}
