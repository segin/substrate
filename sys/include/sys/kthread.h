#ifndef _SYS_KTHREAD_H
#define _SYS_KTHREAD_H

#include <sys/proc.h>

int kthread_create(void (*func)(void *), void *arg, thread_t **tdp, const char *name);

void kthread_exit(void);

#endif
