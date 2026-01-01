#ifndef _PTHREAD_H
#define _PTHREAD_H

#include <stddef.h>

typedef int pthread_t;
typedef int pthread_attr_t;

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start_routine) (void *), void *arg);

void pthread_exit(void *retval);
int pthread_join(pthread_t thread, void **retval);

typedef int pthread_mutex_t;
int pthread_mutex_init(pthread_mutex_t *mutex, const void *attr);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);
int pthread_mutex_destroy(pthread_mutex_t *mutex);

#endif
