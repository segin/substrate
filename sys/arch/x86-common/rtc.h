#ifndef _RTC_H
#define _RTC_H

#include <stdint.h>

// Initialize RTC and set system boot time
#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

#define RTC_SECONDS  0x00
#define RTC_MINUTES  0x02
#define RTC_HOURS    0x04
#define RTC_DAY      0x07
#define RTC_MONTH    0x08
#define RTC_YEAR     0x09
#define RTC_STATUS_A 0x0A
#define RTC_STATUS_B 0x0B

void rtc_init(void);

// Read current time from RTC as Unix timestamp
int64_t rtc_read_time(void);

#endif
