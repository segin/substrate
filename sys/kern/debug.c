#include <sys/proc.h>
#include "../kern/console.h"
#include <stdio.h>
#include "../exec/perso/personality.h"

extern thread_t threads[];
extern thread_t *current_thread;
extern process_t *current_process;

#define MAX_THREADS 64

static const char *state_names[] = {
    "READY", "RUNNING", "BLOCKED", "ZOMBIE"
};

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
    
    kprint("\n TID | PID | State    | Process Name     | Personality | Stack Ptr  | Stack Top\n");
    kprint("-----|-----|----------|------------------|-------------|------------|------------\n");
    
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid == -1) continue;
        
        thread_t *t = &threads[i];
        const char *state = (t->state < 4) ? state_names[t->state] : "???";
        int pid = -1;
        const char *name = "(kernel)";
        const char *pers = "Native";
        
        if (t->proc) {
            pid = t->proc->pid;
            if (t->proc->comm[0]) name = t->proc->comm;
            else name = "(unnamed)";
            if (t->proc->pers && t->proc->pers->name)
                pers = t->proc->pers->name;
        }
        
        sprintf(buf, " %3d | %3d | %s | %s | %s | 0x%08X | 0x%08X\n",
                t->tid, pid, state, name, pers,
                (unsigned int)t->kstack_ptr, (unsigned int)t->kstack_top);
        kprint(buf);
    }
    
    kprint("=== END PROCESS DUMP ===\n\n");
}
