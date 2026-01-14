#include <sys/proc.h>
#include <sys/wait.h>
#include <sys/errno.h>
#include <stddef.h>
#include <kern/sched.h> // for sched_sleep

extern thread_t *current_thread;
extern void sched_sleep(void *chan);

/*
 * find_zombie: Search for a zombie child matching the PID criteria.
 * 
 * Arguments:
 *   pid: Search criteria
 *     > 0: Wait for specific child with PID = pid.
 *     = -1: Wait for any child.
 *     = 0: Wait for any child in the caller's process group.
 *     < -1: Wait for any child in process group = -pid.
 *   parent: The process whose children to search.
 *   out_any_exists: Output pointer, set to 1 if any child matches the criteria (even if not zombie).
 * 
 * Returns:
 *   Pointer to the matching zombie process_t, or NULL if no zombie found.
 */
static process_t *find_zombie(pid_t pid, process_t *parent, int *out_any_exists) {
    if (!parent) return NULL;

    process_t *child = parent->p_children;
    process_t *found = NULL;
    int exists = 0;

    while (child) {
        int match = 0;

        if (pid > 0) {
            if (child->pid == pid) match = 1;
        } else if (pid == -1) {
            match = 1;
        } else if (pid == 0) {
            if (child->pgrp == parent->pgrp) match = 1;
        } else if (pid < -1) {
            if (child->pgrp == -pid) match = 1;
        }
        // Other cases to be implemented


        if (match) {
            exists = 1;
            if (child->state == SZOMB) {
                found = child;
                // BSD/Linux returns the *first* zombie found?
                // Or allows any. We pick the first one.
                break; 
            }
        }
        child = child->p_sibling;
    }

    if (out_any_exists) *out_any_exists = exists;
    return found;
}

int sys_wait4(pid_t pid, int *status, int options, struct rusage *rusage) {
    process_t *cur = current_process;
    process_t *target = NULL;
    int any_exists = 0;

    while (1) {
        // 1. Search Logic
        target = find_zombie(pid, cur, &any_exists);

        if (target) {
            // Found a zombie! Reaping Logic.
            
            // 1. Return Status
            if (status) *status = (target->exit_code << 8); // Simple WEXITSTATUS
            
            // 2. Handle Rusage (Stubbed accumulator for now)
            if (rusage) *rusage = target->rusage;
            
            // Accumulate to parent (Simple stub)
            // cur->rusage_children.ru_utime += target->rusage.ru_utime;
            // cur->rusage_children.ru_stime += target->rusage.ru_stime;

            // 3. Unlink from Parent's List
            if (cur->p_children == target) {
                cur->p_children = target->p_sibling;
            } else {
                process_t *prev = cur->p_children;
                while (prev && prev->p_sibling != target) prev = prev->p_sibling;
                if (prev) prev->p_sibling = target->p_sibling;
            }
            
            // 4. Free Process Slot
            pid_t pid_val = target->pid;
            target->pid = -1; // Mark as free
            target->p_parent = NULL;
            target->p_sibling = NULL;
            target->state = 0; // FREE/SIDL
            
            return pid_val;
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
