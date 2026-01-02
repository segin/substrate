#ifndef _KERN_SCHED_H
#define _KERN_SCHED_H

#include <sys/proc.h>

// API
void sched_init(void);

// Thread Creation
// stack: user stack pointer
// arg: argument for entry point (if supported)
int sched_create_thread(process_t *proc, void (*entry_point)(void*), void *stack, void *arg);

// Fork (Clone Process)
// stack: user stack pointer for child
int sched_fork_process(process_t *parent, void *stack);

void sched_yield(void);
int sched_get_current_tid(void);
void sched_set_priority(int tid, sched_class_t cls, int prio);
void sched_sleep(void *chan);
void sched_wakeup(void *chan);
void sched_wakeup_n(void *chan, int n);
process_t *sched_create_process(struct personality *pers);
thread_t *sched_get_thread(int tid);

#endif
