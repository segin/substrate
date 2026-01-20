#ifndef _SYS_PM_H
#define _SYS_PM_H

#include <sys/proc.h>

#include <sys/lock.h>

#define MAX_PROCS 16

extern process_t processes[MAX_PROCS];
extern process_t *current_process;
extern mutex_t proctree_lock;

void pm_init(void);
process_t *proc_create(struct personality *pers);
int proc_fork(process_t *parent, void *stack);
void proc_remove_child(process_t *parent, process_t *child);

#endif
