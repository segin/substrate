/*
 * <termcap.h> — BSD termcap interface.
 * Substrate stub: forwards to <curses.h> which carries the
 * termcap prototypes (tgetent/tgetnum/tgetstr/tgetflag/tgoto/tputs).
 */
#ifndef _TERMCAP_H
#define _TERMCAP_H
#include <curses.h>

/* BSD termcap declares this global as a side-channel for tgetent. */
extern char PC;
extern char *BC;
extern char *UP;
extern short ospeed;
#endif
