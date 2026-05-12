#ifndef _CTYPE_H
#define _CTYPE_H

#ifdef __cplusplus
extern "C" {
#endif

int isalnum(int c);
int isalpha(int c);
int isblank(int c);
int iscntrl(int c);
int isdigit(int c);
int isgraph(int c);
int islower(int c);
int isprint(int c);
int ispunct(int c);
int isspace(int c);
int isupper(int c);
int isxdigit(int c);
int tolower(int c);
int toupper(int c);

/* SUSv2/XPG4 / SunOS-style isascii.  Standardised by POSIX but
 * marked obsolescent; libstdc++ and a handful of gnulib paths still
 * use it. */
int isascii(int c);
int toascii(int c);

#ifdef __cplusplus
}
#endif
#endif
