#ifndef _KERN_TIME_H
#define _KERN_TIME_H

#include <stdint.h>
#include <sys/types.h>

long get_time(void);
long get_uptime(void);
uint64_t get_ticks(void);

#endif
