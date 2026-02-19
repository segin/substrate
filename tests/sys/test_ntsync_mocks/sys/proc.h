#ifndef _SYS_PROC_H
#define _SYS_PROC_H

typedef struct thread {
    int priority;
} thread_t;

extern thread_t *current_thread;

#endif
