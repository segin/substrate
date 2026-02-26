#ifndef _KERN_SCHED_H
#define _KERN_SCHED_H

#include <stdint.h>
#include <errno.h>

struct thread;
typedef struct thread thread_t;

void sched_wakeup(thread_t *t);
void sched_sleep(thread_t *t);
int sched_sleep_until(thread_t *t, uint64_t deadline);

#endif
