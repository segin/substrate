#include <sys/proc.h>
#include <sys/wait.h>
#include <sys/session.h>
#include <errno.h>
#include <stddef.h>
#include <kern/sched.h> // for sched_sleep
#include <kern/time.h>  // for get_ticks
#include <pm/pm.h>

/* Safety-net poll interval for the blocking wait below.  proc_exit()
 * wakes &parent->p_children, but sched_sleep/sched_wakeup is not
 * fully race-free; a wakeup landing in the wrong window is otherwise
 * lost forever.  Re-checking at this cadence turns that into bounded
 * latency.  ~64ms at HZ=128. */
#define WAIT_POLL_TICKS 8
#include <sys/kern_syscalls.h>
#include <arch/i386/pmap.h>
#include <sys/ldt.h>
#include <vm/vm_map.h>
#include <fs/procfs.h>
#ifndef HOST_TEST
#include <kern/cmdline.h>
#include <kern/console.h>
#endif

#ifdef HOST_TEST
#define PROC_WAIT_DEBUG_ENABLED() 0
#define PROC_WAIT_DEBUG(...) ((void)0)
#else
#define PROC_WAIT_DEBUG_ENABLED() cmdline_debug_enabled("proc")
#define PROC_WAIT_DEBUG(...) kprintf(__VA_ARGS__)
#endif

static int wait_threads_all_zombie(process_t *proc) {
    if (!proc) {
        return 1;
    }

    FOREACH_THREAD(thread) {
        if (thread->proc != proc) {
            continue;
        }
        if (thread->state != THREAD_ZOMBIE) {
            return 0;
        }
    }

    return 1;
}

/*
 * find_waitable_child: Search for a child matching the wait criteria.
 * 
 * Matches zombie children, stopped children (if WUNTRACED), and continued children (if WCONTINUED).
 * 
 * Arguments:
 *   pid: Search criteria
 *   parent: The process whose children to search.
 *   options: Wait options (WNOHANG, WUNTRACED, WCONTINUED)
 *   out_any_exists: Output pointer, set to 1 if any child matches the PID criteria.
 *   out_reason: Output: 0=zombie, 1=stopped, 2=continued
 * 
 * Returns:
 *   Pointer to the matching child process_t, or NULL if none found.
 */
static process_t *find_waitable_child(pid_t pid, process_t *parent, int options, 
                                       int *out_any_exists, int *out_reason) {
    if (!parent) return NULL;

    process_t *child = parent->p_children;
    process_t *found = NULL;
    int exists = 0;
    int reason = 0;

    if (PROC_WAIT_DEBUG_ENABLED()) {
        PROC_WAIT_DEBUG("wait: parent=%d pid=%d children=%p options=%#x\n",
                        parent->pid, pid, parent->p_children, options);
    }

    while (child) {
        int match = 0;

        if (PROC_WAIT_DEBUG_ENABLED()) {
            PROC_WAIT_DEBUG("wait: scan child=%d state=%u flags=%#x sibling=%p parent=%d\n",
                            child->pid, child->state, child->p_flag,
                            child->p_sibling,
                            child->p_parent ? child->p_parent->pid : -1);
        }

        // PID matching logic
        if (pid > 0) {
            if (child->pid == pid) match = 1;
        } else if (pid == -1) {
            match = 1;
        } else if (pid == 0) {
            /* Match if child in same process group as parent */
            int child_pg = child->p_pgrp ? child->p_pgrp->pg_id : 0;
            int parent_pg = parent->p_pgrp ? parent->p_pgrp->pg_id : 0;
            if (child_pg == parent_pg && child_pg != 0) match = 1;
        } else if (pid < -1) {
            /* Match if child in process group = |pid| */
            int child_pg = child->p_pgrp ? child->p_pgrp->pg_id : 0;
            if (child_pg == -pid) match = 1;
        }

        if (match) {
            exists = 1;
            
            // Check for zombie (always reported)
            if (child->state == SZOMB) {
                if (wait_threads_all_zombie(child)) {
                    found = child;
                    reason = 0; // Zombie
                    break;
                }
            }
            
            // Report a stopped child if WUNTRACED was given, OR unconditionally
            // for a traced child: a tracer's wait4() must see every ptrace stop
            // regardless of WUNTRACED (gdb relies on this).
            if (child->state == SSTOP &&
                ((options & WUNTRACED) || (child->p_flag & P_TRACED))) {
                // Only report if not already reported (P_WAITED not set)
                if (!(child->p_flag & P_WAITED)) {
                    found = child;
                    reason = 1; // Stopped
                    break;
                }
            }
            
            // Check for continued (WCONTINUED must be set)
            if ((options & WCONTINUED) && (child->p_flag & P_CONTINUED)) {
                found = child;
                reason = 2; // Continued
                break;
            }
        }
        child = child->p_sibling;
    }

    if (out_any_exists) *out_any_exists = exists;
    if (out_reason) *out_reason = reason;
    if (PROC_WAIT_DEBUG_ENABLED() && !found) {
        PROC_WAIT_DEBUG("wait: no waitable child exists=%d\n", exists);
    }
    return found;
}


/*
 * Atomically find a waitable child and claim the right to report/reap it,
 * all under proctree_lock so concurrent wait4() callers (and the autoreap
 * sweep) cannot select the same child twice.  For a zombie the claim is the
 * unlink from parent->p_children: once unlinked no other waiter's scan can
 * reach it, so exactly one caller runs the teardown — closing the
 * double-reap / double-free (double vm_map_destroy + double kfree of the
 * process_t).  For stopped/continued the report flag is consumed under the
 * lock so only one waiter reports the event.
 *
 * find_waitable_child() (and wait_threads_all_zombie) run under the lock;
 * they only read the tree.  The heavy teardown in the caller runs AFTER the
 * lock is dropped — safe because the corpse is already unreachable.
 */
static process_t *find_and_claim_child(pid_t pid, process_t *parent, int options,
                                       int *any_exists, int *reason) {
    mutex_lock(&proctree_lock);
    process_t *t = find_waitable_child(pid, parent, options, any_exists, reason);
    if (t) {
        if (*reason == 0) {
            /* Claim by unlinking from the parent's child list (inline
             * proc_remove_child — we already hold proctree_lock). */
            if (parent->p_children == t) {
                parent->p_children = t->p_sibling;
            } else {
                process_t *prev = parent->p_children;
                while (prev && prev->p_sibling != t) prev = prev->p_sibling;
                if (prev) prev->p_sibling = t->p_sibling;
            }
            t->p_sibling = NULL;
        } else if (*reason == 1) {
            t->p_flag |= P_WAITED;      /* consume the stop report */
        } else if (*reason == 2) {
            t->p_flag &= ~P_CONTINUED;  /* consume the continue report */
        }
    }
    mutex_unlock(&proctree_lock);
    return t;
}

/* Non-claiming, lock-protected peek used by the pre-block recheck: reads the
 * tree consistently against a concurrent claim+free without unlinking. */
static process_t *peek_waitable_child(pid_t pid, process_t *parent, int options,
                                      int *any_exists, int *reason) {
    mutex_lock(&proctree_lock);
    process_t *t = find_waitable_child(pid, parent, options, any_exists, reason);
    mutex_unlock(&proctree_lock);
    return t;
}

int kern_wait4(pid_t pid, int *status, int options, struct rusage *rusage) {
    process_t *cur = current_process;
    process_t *target = NULL;
    int any_exists = 0;
    int reason = 0;

    while (1) {
        // 1. Search + atomic claim - handles zombies, stopped, and continued
        target = find_and_claim_child(pid, cur, options, &any_exists, &reason);

        if (target) {
            pid_t pid_val = target->pid;
            
            switch (reason) {
            case 0: // Zombie - reap it
                if (status) {
                    if (target->p_flag & P_SIGEXIT) {
                        *status = target->exit_code;
                    } else {
                        *status = ((target->exit_code & 0xff) << 8);
                    }
                }
                
                // Handle Rusage - Copy to user buffer and accumulate to parent
                if (rusage) *rusage = target->rusage;
                
                // Accumulate child's rusage into parent's rusage_children (BSD semantics)
                cur->rusage_children.ru_utime.tv_sec += target->rusage.ru_utime.tv_sec;
                cur->rusage_children.ru_utime.tv_usec += target->rusage.ru_utime.tv_usec;
                if (cur->rusage_children.ru_utime.tv_usec >= 1000000) {
                    cur->rusage_children.ru_utime.tv_sec++;
                    cur->rusage_children.ru_utime.tv_usec -= 1000000;
                }
                cur->rusage_children.ru_stime.tv_sec += target->rusage.ru_stime.tv_sec;
                cur->rusage_children.ru_stime.tv_usec += target->rusage.ru_stime.tv_usec;
                if (cur->rusage_children.ru_stime.tv_usec >= 1000000) {
                    cur->rusage_children.ru_stime.tv_sec++;
                    cur->rusage_children.ru_stime.tv_usec -= 1000000;
                }
                // Also accumulate grandchildren's usage
                cur->rusage_children.ru_maxrss = (target->rusage_children.ru_maxrss > cur->rusage_children.ru_maxrss) 
                    ? target->rusage_children.ru_maxrss : cur->rusage_children.ru_maxrss;
                /* rusage_finalize() already folded target->rusage_children into
                 * target->rusage at exit, so target->rusage.ru_* includes the
                 * grandchildren's counts.  Adding rusage_children again here
                 * would double-count them (compounding per generation); mirror
                 * the utime/stime accumulation above and take target->rusage
                 * only. */
                cur->rusage_children.ru_minflt += target->rusage.ru_minflt;
                cur->rusage_children.ru_majflt += target->rusage.ru_majflt;
                cur->rusage_children.ru_nvcsw += target->rusage.ru_nvcsw;
                cur->rusage_children.ru_nivcsw += target->rusage.ru_nivcsw;

                // Already unlinked from the parent's child list by
                // find_and_claim_child (the atomic claim); just clear
                // process group membership now.
                pgrp_remove_proc(target);

                if (target->vm_map) {
                    vm_map_destroy(target->vm_map);
                    target->vm_map = NULL;
                } else if (target->pmap && target->pmap != pmap_kernel()) {
                    pmap_release(target->pmap);
                }
                target->pmap = pmap_kernel();
                ldt_free_process(target);

                // Retire all thread slots that belonged to the reaped process.
                sched_reap_process_threads(target);

                // Release the lazy /proc/<pid>/* nodes (~10 KB each) that
                // procfs synthesised for this pid.  Without this every
                // exec'd process permanently leaks its procfs entry.
                procfs_release_pid_nodes(target->pid);

                /* Fully release the process_t: unlink from allproc +
                 * pid_hash and free storage.  Previously we just set
                 * target->pid = -1 and left the entry linked, which
                 * meant sys_proc_list kept reporting a stale -1
                 * entry (visible in ps as a phantom row when paired
                 * with sys_proc_info's pid=0 → "self" sentinel).
                 * proc_destroy is the canonical teardown. */
                proc_destroy(target);

                return pid_val;
                
            case 1: // Stopped (WUNTRACED)
                // Return stopped status: 0x7f in low byte, stop signal in high byte
                // (P_WAITED already set by find_and_claim_child)
                {
                    int stopsig = target->p_xsig ? target->p_xsig : SIGSTOP;
                    if (status) *status = 0x7f | (stopsig << 8);
                }
                return pid_val;

            case 2: // Continued (WCONTINUED)
                // Return continued status: 0xffff (P_CONTINUED already cleared
                // by find_and_claim_child)
                if (status) *status = 0xffff; // WIFCONTINUED will be true
                return pid_val;
            }
        }


        if (!any_exists) {
            return -ECHILD;
        }

        if (options & WNOHANG) {
            return 0;
        }

        // Blocking Wait
        // Check for signals first (EINTR)
        if (current_thread->sig_pending & ~current_thread->sig_mask) {
            return -EINTR;
        }

        /*
         * Recheck under proctree_lock before blocking, to avoid missed
         * wakeups: a child exit can race the gap between the scan above and
         * blocking.  The peek takes a (sleepable) mutex, so it must run
         * BEFORE we mark the thread BLOCKED — otherwise mutex contention
         * inside the peek would clobber that state.  The (now slightly wider)
         * lost-wakeup window is covered by the sleep_expiry deadline below.
         */
        target = peek_waitable_child(pid, cur, options, &any_exists, &reason);
        if (target || !any_exists ||
            (current_thread->sig_pending & ~current_thread->sig_mask)) {
            continue;
        }

        current_thread->wait_chan = &cur->p_children;
        current_thread->state = THREAD_BLOCKED;

        /* Block until proc_exit() wakes &p_children — but arm a
         * deadline so a missed wakeup re-checks instead of wedging
         * forever (this hung init at the first rc.d script). */
        current_thread->sleep_expiry = get_ticks() + WAIT_POLL_TICKS;
        sched_yield();
        current_thread->sleep_expiry = 0;
        current_thread->wait_chan = NULL;
        current_thread->state = THREAD_READY;
    }
}

int sys_wait4(pid_t pid, int *status, int options, struct rusage *rusage) {
    int kstatus = 0;
    struct rusage krusage;
    int ret = kern_wait4(pid, status ? &kstatus : NULL, options, rusage ? &krusage : NULL);

    if (ret >= 0) {
        if (status && copyout(&kstatus, status, sizeof(int)) != 0) return -EFAULT;
        if (rusage && copyout(&krusage, rusage, sizeof(struct rusage)) != 0) return -EFAULT;
    }

    return ret;
}
