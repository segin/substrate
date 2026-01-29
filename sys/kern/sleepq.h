/*
 * sleepq.h - Sleep Queue Interface
 */

#ifndef _KERN_SLEEPQ_H
#define _KERN_SLEEPQ_H

#include <sys/types.h>
#include <sys/proc.h>

void sleepq_init(void);
void sleepq_add(void *chan, thread_t *t);
thread_t *sleepq_wake_one(void *chan);
int sleepq_wake_all(void *chan);
int sleepq_wake_n(void *chan, int n);
int sleepq_has_waiters(void *chan);
int sleepq_requeue(void *src_chan, void *dst_chan, int wake_n, int requeue_n);

#endif /* _KERN_SLEEPQ_H */
