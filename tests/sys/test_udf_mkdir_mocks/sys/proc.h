#ifndef _SYS_PROC_H
#define _SYS_PROC_H

#include <stdint.h>

struct process {
    uint32_t uid;
    uint32_t gid;
};

struct thread {
    uint64_t sleep_expiry;
    int sleep_status;
};

extern struct process *current_process;
extern struct thread *current_thread;

#endif
