#include <sys/proc.h>
#include "../kern/console.h"
#include <stdio.h>

extern thread_t threads[];
extern thread_t *current_thread;
extern process_t *current_process;

#define MAX_THREADS 64

static const char *state_names[] = {
    "READY", "RUNNING", "BLOCKED", "ZOMBIE"
};

void debug_dump_processes(void) {
    kprint("\n=== PROCESS TABLE DUMP (Ctrl+F9) ===\n");
    
    char buf[80];
    
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
    
    kprint("\n TID | PID | State    | Priority | Stack Ptr  | Stack Top\n");
    kprint("-----|-----|----------|----------|------------|------------\n");
    
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid == -1) continue;
        
        thread_t *t = &threads[i];
        const char *state = (t->state < 4) ? state_names[t->state] : "???";
        int pid = t->proc ? t->proc->pid : -1;
        
        sprintf(buf, " %3d | %3d | %-8s | %8d | 0x%08X | 0x%08X\n",
                t->tid, pid, state, t->priority,
                (unsigned int)t->kstack_ptr, (unsigned int)t->kstack_top);
        kprint(buf);
    }
    
    kprint("=== END PROCESS DUMP ===\n\n");
}
