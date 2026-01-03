#ifndef _RTC_H
#define _RTC_H

#include <stdint.h>

// Initialize RTC and set system boot time
void rtc_init(void);

// Read current time from RTC as Unix timestamp
int64_t rtc_read_time(void);

#endif
