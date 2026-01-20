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

void proc_set_bitness(process_t *p, uint8_t bitness);
uint8_t proc_get_bitness(process_t *p);


#endif
