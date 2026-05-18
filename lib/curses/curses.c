/*
 * libcurses — stub terminfo/termcap surface.
 *
 * Substrate doesn't ship a real ncurses port (yet).  This stub
 * exists so that link-time probes from autoconf-style configure
 * scripts (notably zsh's `--with-term-lib` lookup) find the
 * usual symbol set and link succeeds.  At runtime every function
 * here returns the "no terminal info available" sentinel.
 * Programs that genuinely depend on terminfo (full-screen editors,
 * line-editing layers, pagers) will not render correctly under
 * this stub — but pure script execution works.
 *
 * The intent is to upgrade this to a real terminfo backend once
 * the substrate /etc/terminfo path and a parser exist.  Until
 * then, every entry point is intentionally minimal.
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* terminfo capability-query family — return "missing" sentinels. */

int
tigetflag(const char *capname)
{
    (void)capname;
    return -1;  /* "absent" */
}

int
tigetnum(const char *capname)
{
    (void)capname;
    return -2;  /* "absent" */
}

char *
tigetstr(const char *capname)
{
    (void)capname;
    return (char *)-1;  /* "absent" */
}

/* termcap legacy interface. */

int
tgetent(char *bp, const char *name)
{
    (void)bp;
    (void)name;
    return 0;  /* "no entry found" — not -1 (error), not 1 (success). */
}

int
tgetflag(const char *id)
{
    (void)id;
    return 0;
}

int
tgetnum(const char *id)
{
    (void)id;
    return -1;
}

char *
tgetstr(const char *id, char **area)
{
    (void)id;
    (void)area;
    return NULL;
}

char *
tgoto(const char *cap, int col, int row)
{
    (void)cap;
    (void)col;
    (void)row;
    return (char *)"";
}

char *
tparm(const char *str, ...)
{
    (void)str;
    return (char *)"";
}

int
tputs(const char *str, int affcnt, int (*putc_fn)(int))
{
    (void)affcnt;
    if (str && putc_fn) {
        while (*str) {
            putc_fn((unsigned char)*str++);
        }
    }
    return 0;
}

int
setupterm(const char *term, int filedes, int *errret)
{
    (void)term;
    (void)filedes;
    if (errret) {
        *errret = 0;  /* "no terminfo entry" — callers must cope. */
    }
    return -1;  /* ERR */
}

int
del_curterm(void *t)
{
    (void)t;
    return 0;
}

void *
set_curterm(void *t)
{
    (void)t;
    return NULL;
}

int
restartterm(const char *term, int filedes, int *errret)
{
    return setupterm(term, filedes, errret);
}

int
putp(const char *str)
{
    if (str) {
        while (*str) {
            putchar((unsigned char)*str++);
        }
    }
    return 0;
}

/* tparm with up to 9 long args — the ABI form used by zsh and
 * other terminfo consumers. */
char *
tiparm(const char *str, ...)
{
    (void)str;
    return (char *)"";
}

/* Globals the curses ABI defines.  Real ncurses uses thread-local
 * indirection; for the stub a single instance is fine since nothing
 * useful happens. */
int  COLS = 80;
int  LINES = 24;
/* BSD termcap side-channel globals — referenced by callers that
 * include <termcap.h>. */
char  PC      = 0;
char *BC      = NULL;
char *UP      = NULL;
short ospeed  = 0;
/* Real ncurses exposes cur_term as the currently-selected TERMINAL*.
 * Stub: keep it NULL — callers that actually deref it can crash;
 * the curses front-end is itself a stub. */
void *cur_term = NULL;

char *boolnames[1]  = { NULL };
char *boolcodes[1]  = { NULL };
char *boolfnames[1] = { NULL };
char *numnames[1]   = { NULL };
char *numcodes[1]   = { NULL };
char *numfnames[1]  = { NULL };
char *strnames[1]   = { NULL };
char *strcodes[1]   = { NULL };
char *strfnames[1]  = { NULL };
