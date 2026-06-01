#ifndef _LANGINFO_H
#define _LANGINFO_H

/*
 * langinfo.h - language information constants (POSIX nl_langinfo).
 *
 * Substrate's libc is UTF-8 throughout; the chief consumer of this
 * interface is the CODESET query, which programs (ncurses, less,
 * xterm, vim, ...) use to decide whether to run in UTF-8 mode.  The
 * remaining items return en_US / C "POSIX" values.  The numeric
 * item codes below are private to substrate (header and
 * implementation move together) — do not assume glibc compatibility.
 */

#include <locale.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int nl_item;

#define CODESET      1   /* character encoding name, e.g. "UTF-8" */

#define D_T_FMT      2   /* date+time format (%c) */
#define D_FMT        3   /* date format (%x) */
#define T_FMT        4   /* time format (%X) */
#define T_FMT_AMPM   5   /* 12-hour time format */
#define AM_STR       6
#define PM_STR       7

#define DAY_1        8   /* Sunday */
#define DAY_2        9
#define DAY_3        10
#define DAY_4        11
#define DAY_5        12
#define DAY_6        13
#define DAY_7        14

#define ABDAY_1      15
#define ABDAY_2      16
#define ABDAY_3      17
#define ABDAY_4      18
#define ABDAY_5      19
#define ABDAY_6      20
#define ABDAY_7      21

#define MON_1        22  /* January */
#define MON_2        23
#define MON_3        24
#define MON_4        25
#define MON_5        26
#define MON_6        27
#define MON_7        28
#define MON_8        29
#define MON_9        30
#define MON_10       31
#define MON_11       32
#define MON_12       33

#define ABMON_1      34
#define ABMON_2      35
#define ABMON_3      36
#define ABMON_4      37
#define ABMON_5      38
#define ABMON_6      39
#define ABMON_7      40
#define ABMON_8      41
#define ABMON_9      42
#define ABMON_10     43
#define ABMON_11     44
#define ABMON_12     45

#define RADIXCHAR    46  /* decimal point */
#define THOUSEP      47  /* thousands separator */
#define YESEXPR      48  /* affirmative response regex */
#define NOEXPR       49  /* negative response regex */
#define CRNCYSTR     50  /* currency symbol */

char *nl_langinfo(nl_item item);

#ifdef __cplusplus
}
#endif

#endif /* _LANGINFO_H */
