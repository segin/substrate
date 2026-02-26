#ifndef _KERN_SCHED_H
#define _KERN_SCHED_H

#include <stdint.h>

void sched_get_loadavg(unsigned long loads[]);
uint32_t sched_count_runnable(void);
uint32_t sched_count_threads(void);

#define LOAD_INT(x) ((x) >> 16)
#define LOAD_FRAC(x) (((x) & 0xFFFF))

#endif
