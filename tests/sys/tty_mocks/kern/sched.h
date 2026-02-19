#pragma once
struct thread {
    int state;
    void *wait_chan;
};
#define THREAD_BLOCKED 1
extern struct thread *current_thread;
void sched_yield(void);
void sched_wakeup(void *chan);
