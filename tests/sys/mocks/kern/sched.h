#ifndef _KERN_SCHED_H
#define _KERN_SCHED_H
void sched_wakeup(void *chan);
void sched_sleep(void *chan);
#endif
