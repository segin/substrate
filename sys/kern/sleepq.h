/*
 * sleepq.h - Sleep Queue Interface
 */

#ifndef _KERN_SLEEPQ_H
#define _KERN_SLEEPQ_H

#include <sys/types.h>
#include <sys/proc.h>

/* Sleep Queue Types */
#define SLEEPQ_TYPE_SHARED  0
#define SLEEPQ_TYPE_PRIVATE 1

void sleepq_init(void);

/* Standard (Shared/Global) Sleep Queue Operations */
void sleepq_add(void *chan, thread_t *t);
thread_t *sleepq_wake_one(void *chan);
int sleepq_wake_all(void *chan);
int sleepq_wake_n(void *chan, int n);
int sleepq_has_waiters(void *chan);

/* Private (Process-Local) Sleep Queue Operations */
void sleepq_add_private(void *chan, thread_t *t);
int sleepq_wake_n_private(void *chan, int n);

/* Requeue Operation (supports both types via flag) */
int sleepq_requeue(void *src_chan, void *dst_chan, int wake_n, int requeue_n, int private_op);

#endif /* _KERN_SLEEPQ_H */
