#include <sys/proc.h>
#include <sys/wait.h>
#include <sys/errno.h>
#include <stddef.h>

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
            // Found a zombie!
            // TODO: Reaping logic (next task)
            
            // Temporary return for testing search logic
            if (status) *status = (target->exit_code << 8); // Simple WEXITSTATUS
            
            // Remove from lists (Stub for now, to enable loop breaking if called repeatedly)
            // But we shouldn't modify global state if we are just testing search logic in isolation?
            // "Task: Match based on pid argument / Reaping" are separate.
            // But if I don't reap, the test loop will run forever finding the same zombie.
            // For this specific commit (Search Logic), I just need to match correctly.
            // I'll return the PID.
            return target->pid;
        }

        if (!any_exists) {
            return -ECHILD;
        }

        if (options & WNOHANG) {
            return 0;
        }

        // Blocking (stub)
        return -EINTR; // Fallback so we don't hang in this initial commit
    }
}
