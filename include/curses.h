/*
 * <curses.h> — minimal terminfo/termcap surface (substrate stub).
 *
 * Substrate's libcurses is currently a no-op stub; this header
 * exposes only the symbols configure-time probes (zsh, libedit
 * upstream, less, etc.) tend to test against.  Full ncurses port
 * is a future task.
 */
#ifndef _CURSES_H
#define _CURSES_H

#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ERR (-1)
#define OK   (0)

/* terminfo */
int   tigetflag(const char *capname);
int   tigetnum(const char *capname);
char *tigetstr(const char *capname);

/* termcap */
int   tgetent(char *bp, const char *name);
int   tgetflag(const char *id);
int   tgetnum(const char *id);
char *tgetstr(const char *id, char **area);
char *tgoto(const char *cap, int col, int row);

/* terminfo formatting */
char *tparm(const char *str, ...);
char *tiparm(const char *str, ...);
int   tputs(const char *str, int affcnt, int (*putc_fn)(int));
int   putp(const char *str);

/* setup */
int   setupterm(const char *term, int filedes, int *errret);
int   del_curterm(void *t);
void *set_curterm(void *t);
int   restartterm(const char *term, int filedes, int *errret);

/* globals */
extern int   COLS;
extern int   LINES;
extern void *cur_term;
extern char *boolnames[];
extern char *boolcodes[];
extern char *boolfnames[];
extern char *numnames[];
extern char *numcodes[];
extern char *numfnames[];
extern char *strnames[];
extern char *strcodes[];
extern char *strfnames[];

#ifdef __cplusplus
}
#endif

#endif /* _CURSES_H */
