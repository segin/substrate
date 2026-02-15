#ifndef _SYS_PM_H
#define _SYS_PM_H

#include <stdint.h>
#include <stddef.h>

#define MAX_PROCS 16

typedef int pid_t;

typedef struct process {
    pid_t pid;
    // other fields unused by sysinfo
} process_t;

extern process_t processes[MAX_PROCS];

#endif
