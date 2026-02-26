#ifndef _SYS_PROC_H
#define _SYS_PROC_H

#include <stdint.h>
#include <kern/file.h>
#include <kern/sched.h>
#include <vfs/vfs.h>

#define MAX_FD 32

typedef struct process {
    file_t *fds[MAX_FD];
    struct thread *thread;
} process_t;

typedef struct thread {
    int priority;
} thread_t;

extern process_t *current_process;
extern thread_t *current_thread;

int proc_alloc_fd(process_t *p);
void proc_set_fd(process_t *p, int fd, file_t *f);
void proc_clear_fd(process_t *p, int fd);

#endif
