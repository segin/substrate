#ifndef _SYS_PM_H
#define _SYS_PM_H

#include <sys/proc.h>

#include <sys/lock.h>

#define MAX_PROCS 16

extern process_t processes[MAX_PROCS];
extern process_t *current_process;
extern mutex_t proctree_lock;

void pm_init(void);
process_t *proc_create(int perso_id);
int proc_fork(process_t *parent, void *stack);
int proc_vfork(process_t *parent, void *stack);
void proc_remove_child(process_t *parent, process_t *child);
int proc_begin_vfork(process_t *child);
void proc_vfork_done(process_t *child);

void proc_set_bitness(process_t *p, uint8_t bitness);
uint8_t proc_get_bitness(process_t *p);
void proc_capture_cmdline(process_t *p, char *const argv[]);
size_t proc_emit_cmdline(const process_t *p, char *buf, size_t buf_len, size_t *argc_out);

/*
 * proc_find - Find a process by PID
 *
 * Returns a pointer to the process structure for the given PID,
 * or NULL if no such process exists.
 *
 * Note: The returned pointer is valid only while proctree_lock is held
 * or when called from interrupt context with interrupts disabled.
 */
process_t *proc_find(int pid);

int proc_get_last_pid(void);
void proc_reap_autoreap_zombies(void);

#endif
