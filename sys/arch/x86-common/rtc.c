#include <rtc.h>
#include "io.h"
#include <kern/console.h>
#include <stdint.h>

extern int64_t boot_time;

static uint8_t rtc_read_reg(uint8_t reg) {
    outb(CMOS_ADDRESS, reg);
    return inb(CMOS_DATA);
}

static int rtc_is_updating(void) {
    outb(CMOS_ADDRESS, RTC_STATUS_A);
    return (inb(CMOS_DATA) & 0x80);
}

static uint8_t bcd_to_binary(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

// Days in month for non-leap and leap years
static const int days_in_month[2][12] = {
    {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
    {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}
};

static int is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

// Convert date/time to Unix timestamp (seconds since Jan 1, 1970)
static int64_t to_unix_time(int year, int month, int day, int hour, int min, int sec) {
    int64_t days = 0;
    
    // Count days from 1970 to year-1
    for (int y = 1970; y < year; y++) {
        days += is_leap_year(y) ? 366 : 365;
    }
    
    // Add days for completed months in current year
    int leap = is_leap_year(year);
    for (int m = 1; m < month; m++) {
        days += days_in_month[leap][m - 1];
    }
    
    // Add days in current month
    days += day - 1;
    
    // Convert to seconds
    int64_t seconds = days * 86400LL + hour * 3600LL + min * 60LL + sec;
    
    return seconds;
}

int64_t rtc_read_time(void) {
    uint8_t sec, min, hour, day, month, year;
    uint8_t last_sec, last_min, last_hour, last_day, last_month, last_year;
    uint8_t status_b;
    
    // Wait until RTC is not updating
    while (rtc_is_updating());
    
    // Read twice to ensure consistency
    do {
        last_sec = rtc_read_reg(RTC_SECONDS);
        last_min = rtc_read_reg(RTC_MINUTES);
        last_hour = rtc_read_reg(RTC_HOURS);
        last_day = rtc_read_reg(RTC_DAY);
        last_month = rtc_read_reg(RTC_MONTH);
        last_year = rtc_read_reg(RTC_YEAR);
        
        while (rtc_is_updating());
        
        sec = rtc_read_reg(RTC_SECONDS);
        min = rtc_read_reg(RTC_MINUTES);
        hour = rtc_read_reg(RTC_HOURS);
        day = rtc_read_reg(RTC_DAY);
        month = rtc_read_reg(RTC_MONTH);
        year = rtc_read_reg(RTC_YEAR);
    } while (sec != last_sec || min != last_min || hour != last_hour ||
             day != last_day || month != last_month || year != last_year);
    
    // Check if values are in BCD format
    status_b = rtc_read_reg(RTC_STATUS_B);
    if (!(status_b & 0x04)) {
        // BCD mode - convert to binary
        sec = bcd_to_binary(sec);
        min = bcd_to_binary(min);
        hour = bcd_to_binary(hour);
        day = bcd_to_binary(day);
        month = bcd_to_binary(month);
        year = bcd_to_binary(year);
    }
    
    // Convert 2-digit year to 4-digit year (assume 2000-2099)
    int full_year = 2000 + year;
    
    return to_unix_time(full_year, month, day, hour, min, sec);
}

void rtc_init(void) {
    int64_t current_time = rtc_read_time();
    boot_time = current_time;
    
    kprint("RTC: Hardware clock initialized\n");
}
