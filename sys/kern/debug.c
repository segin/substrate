#include <sys/proc.h>
#include <kern/sched.h>
#include <kern/console.h>
#include <stdio.h>
#include <exec/perso/personality.h>
#include <string.h>

extern thread_t *current_thread;
extern process_t *current_process;

static const char *state_names[] = {
    "READY", "RUNNING", "BLOCKED", "ZOMBIE"
};

static void dump_thread_callback(thread_t *t, void *arg) {
    (void)arg;
    char buf[128];
    const char *state = (t->state < 4) ? state_names[t->state] : "???";
    int pid = -1;
    static char name_buf[24];
    const char *name = "(kernel)";
    const char *pers = "(kernel)";
    
    if (t->proc) {
        pid = t->proc->pid;
        if (t->proc->comm[0]) {
            // Wrap kernel task names in parentheses
            if (t->proc->is_kernel_task) {
                sprintf(name_buf, "(%s)", t->proc->comm);
                name = name_buf;
            } else {
                name = t->proc->comm;
            }
        } else {
            name = "(unnamed)";
        }
        // Kernel tasks show "(kernel)" instead of personality
        if (t->proc->is_kernel_task) {
            pers = "(kernel)";
        } else if (t->proc->pers && t->proc->pers->name) {
            // Inspecting kernel personality structure (read-only metadata)
            pers = t->proc->pers->name;
        }
    }
    
    const char *reason = t->wait_reason ? t->wait_reason : "";

    sprintf(buf, " %5d | %5d | %-8.8s | %-16.16s | %-9.9s | %s\n",
            t->tid, pid, state, name, pers, reason);
    kprint(buf);
}

void debug_dump_processes(void) {
    kprint("\n=== PROCESS TABLE DUMP (Ctrl+F9) ===\n");
    
    char buf[128];
    
    // Current thread info
    if (current_thread) {
        sprintf(buf, "Current Thread: TID=%d\n", current_thread->tid);
        kprint(buf);
    }
    if (current_process) {
        sprintf(buf, "Current Process: PID=%d (%s)\n", 
                current_process->pid, current_process->comm);
        kprint(buf);
    }
    
    kprint("\n   TID |   PID | STATE    | NAME             | PERSO     | WAIT REASON\n");
    kprint("-------|-------|----------|------------------|-----------|------------\n");
    
    sched_iterate_threads(dump_thread_callback, NULL);
    
    kprint("=== END PROCESS DUMP ===\n\n");
}
