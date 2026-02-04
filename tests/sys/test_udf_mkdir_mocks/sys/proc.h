#ifndef _SYS_PROC_H
#define _SYS_PROC_H

#include <stdint.h>

struct process {
    uint32_t uid;
    uint32_t gid;
};

extern struct process *current_process;

#endif
