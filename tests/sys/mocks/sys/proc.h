#ifndef _SYS_PROC_H
#define _SYS_PROC_H

#include <vfs/vfs.h>
#include <stdint.h>
#include <stddef.h>

struct pmap;
typedef struct pmap *pmap_t;
struct robust_list_head;

typedef struct process {
    fs_node_t *cwd_node;
    fs_node_t *root_node;
    /* Added for futex tests */
    uint32_t uid;
    uint32_t euid;
    pmap_t pmap;
} process_t;

typedef int sched_class_t;
#define THREAD_F_INTERRUPTIBLE 0x0001

typedef struct thread {
    uint64_t sleep_expiry;
    int sleep_status;
    /* Added for futex tests */
    int tid;
    struct process *proc;
    struct robust_list_head *robust_list;
    size_t robust_list_len;
    int priority;
    sched_class_t sched_class;

    /* Signals */
    uint32_t flags;
    uint32_t sig_pending;
    uint32_t sig_mask;
} thread_t;

extern process_t *current_process;
extern thread_t *current_thread;

#endif
