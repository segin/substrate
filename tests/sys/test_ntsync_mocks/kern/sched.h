#ifndef _KERN_SCHED_H
#define _KERN_SCHED_H

#include <stdint.h>
#include <sys/proc.h>

void sched_wakeup(void *t);
void sched_sleep(void *chan);
int sched_sleep_until(void *t, uint64_t deadline);

#endif
