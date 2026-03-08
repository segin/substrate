#include <sys/proc.h>
#include <sys/wait.h>
#include <sys/session.h>
#include <errno.h>
#include <stddef.h>
#include <kern/sched.h> // for sched_sleep
#include <pm/pm.h>
#include <sys/kern_syscalls.h>

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

    while (child) {
        int match = 0;

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
                found = child;
                reason = 0; // Zombie
                break;
            }
            
            // Check for stopped (WUNTRACED must be set)
            if ((options & WUNTRACED) && child->state == SSTOP) {
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
    return found;
}


int kern_wait4(pid_t pid, int *status, int options, struct rusage *rusage) {
    process_t *cur = current_process;
    process_t *target = NULL;
    int any_exists = 0;
    int reason = 0;

    while (1) {
        // 1. Search Logic - now handles zombies, stopped, and continued
        target = find_waitable_child(pid, cur, options, &any_exists, &reason);

        if (target) {
            pid_t pid_val = target->pid;
            
            switch (reason) {
            case 0: // Zombie - reap it
                // Return Status (exited: low 7 bits = 0, high 8 bits = exit code)
                if (status) *status = (target->exit_code << 8);
                
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
                cur->rusage_children.ru_minflt += target->rusage.ru_minflt + target->rusage_children.ru_minflt;
                cur->rusage_children.ru_majflt += target->rusage.ru_majflt + target->rusage_children.ru_majflt;
                cur->rusage_children.ru_nvcsw += target->rusage.ru_nvcsw + target->rusage_children.ru_nvcsw;
                cur->rusage_children.ru_nivcsw += target->rusage.ru_nivcsw + target->rusage_children.ru_nivcsw;

                // Unlink from Parent's List
                // Unlink from Parent's List
                extern void proc_remove_child(process_t *parent, process_t *child);
                proc_remove_child(cur, target);
                
                // Clear process group membership
                extern void pgrp_remove_proc(struct process *proc);
                pgrp_remove_proc(target);

                // Retire all thread slots that belonged to the reaped process.
                sched_reap_process_threads(target);
                
                // Free Process Slot
                target->pid = -1;
                target->p_parent = NULL;
                target->p_sibling = NULL;
                target->state = 0;
                
                return pid_val;
                
            case 1: // Stopped (WUNTRACED)
                // Return stopped status: 0x7f in low byte, stop signal in high byte
                // For now, use SIGSTOP (19) as the default stop signal
                if (status) *status = 0x7f | (19 << 8); // WIFSTOPPED will be true
                // Mark as reported so we don't report again
                target->p_flag |= P_WAITED;
                return pid_val;
                
            case 2: // Continued (WCONTINUED)
                // Return continued status: 0xffff
                if (status) *status = 0xffff; // WIFCONTINUED will be true
                // Clear P_CONTINUED flag so we don't report again
                target->p_flag &= ~P_CONTINUED;
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

        // Sleep on p_children channel
        // current_process->p_children address is unique to this process
        sched_sleep(&cur->p_children);
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
