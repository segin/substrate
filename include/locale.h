#ifndef _LOCALE_H
#define _LOCALE_H

#ifdef __cplusplus
extern "C" {
#endif

#define LC_ALL      0
#define LC_COLLATE  1
#define LC_CTYPE    2
#define LC_MONETARY 3
#define LC_NUMERIC  4
#define LC_TIME     5
#define LC_MESSAGES 6

struct lconv {
    char *decimal_point;
    char *thousands_sep;
    char *grouping;
    char *int_curr_symbol;
    char *currency_symbol;
    char *mon_decimal_point;
    char *mon_thousands_sep;
    char *mon_grouping;
    char *positive_sign;
    char *negative_sign;
    char  int_frac_digits;
    char  frac_digits;
    char  p_cs_precedes;
    char  p_sep_by_space;
    char  n_cs_precedes;
    char  n_sep_by_space;
    char  p_sign_posn;
    char  n_sign_posn;
};

/*
 * POSIX puts locale_t in <locale.h>.  Substrate has no locale machinery, so
 * it is an opaque pointer; the type exists so that strerror_l(3) and friends
 * can be declared and defined without each one inventing its own typedef --
 * which is what src/string.c used to do, and which collided with glibc's
 * declaration whenever that file was compiled natively for the host tests.
 */
#ifndef __locale_t_defined
#define __locale_t_defined 1
typedef void *locale_t;
#endif

char *setlocale(int category, const char *locale);
struct lconv *localeconv(void);

#ifdef __cplusplus
}
#endif

#endif /* _LOCALE_H */
