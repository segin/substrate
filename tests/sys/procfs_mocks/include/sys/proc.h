#ifndef _SYS_PROC_H
#define _SYS_PROC_H

#include <stdint.h>
#include <sys/types.h>

#define MAX_PROCS 32
#define AC_COMM_LEN 16

typedef struct process {
    int pid;
    int uid;
    int gid;
    char comm[AC_COMM_LEN];
    int perso_id;
} process_t;

process_t *proc_find(int pid);
int proc_get_last_pid(void);

extern process_t processes[MAX_PROCS];
extern process_t *current_process;

#endif
