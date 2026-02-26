#ifndef _SYS_TIME_H
#define _SYS_TIME_H

#include <stdint.h>

int64_t get_time(void);
uint64_t get_uptime(void);
uint64_t get_ticks(void);

#endif
