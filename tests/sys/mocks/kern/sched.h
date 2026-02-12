#ifndef _KERN_SCHED_H
#define _KERN_SCHED_H
void sched_sleep(void *chan);
void sched_set_priority(int tid, int cls, int prio);
void sched_wakeup(void *chan);
struct thread;
struct thread *sched_get_thread(int tid);
#endif
