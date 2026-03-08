#ifndef _KERN_SCHED_H
#define _KERN_SCHED_H
void sched_wakeup(void *chan);
void sched_sleep(void *chan);
void sched_yield(void);
struct thread;
struct thread *sched_get_thread(int tid);
void sched_set_priority(int tid, int cls, int prio);
#endif
