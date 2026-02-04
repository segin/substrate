#ifndef _SYS_PROC_H
#define _SYS_PROC_H

#include <vfs/vfs.h>

typedef struct process {
    fs_node_t *cwd_node;
    fs_node_t *root_node;
} process_t;

typedef struct thread {
    uint64_t sleep_expiry;
    int sleep_status;
} thread_t;

extern process_t *current_process;
extern thread_t *current_thread;

#endif
