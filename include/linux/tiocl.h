#ifndef _LINUX_TIOCL_H
#define _LINUX_TIOCL_H

/* Console TIOCLINUX sub-commands (the second byte passed to ioctl(TIOCLINUX)).
 * Matches the Linux UAPI header so console-aware userland (SDL2's evdev
 * keyboard shift-state query, gpm, ...) compiles and uses the same numbers. */

#define TIOCL_SETSEL		2	/* set a selection */
# define TIOCL_SELCHAR		0	/* select characters */
# define TIOCL_SELWORD		1	/* select whole words */
# define TIOCL_SELLINE		2	/* select whole lines */
# define TIOCL_SELPOINTER	3	/* show the pointer */
# define TIOCL_SELCLEAR		4	/* clear visibility of selection */
# define TIOCL_SELMOUSEREPORT	16	/* report beginning of selection */
# define TIOCL_SELBUTTONMASK	15	/* button mask for report */
#define TIOCL_PASTESEL		3	/* paste previous selection */
#define TIOCL_UNBLANKSCREEN	4	/* unblank screen */
#define TIOCL_SELLOADLUT	5	/* set characters to be considered alphabetic */
#define TIOCL_GETSHIFTSTATE	6	/* get shift state */
#define TIOCL_GETMOUSEREPORTING	7	/* get mouse reporting mode */
#define TIOCL_SETVESABLANK	10	/* set vesa blanking mode */
#define TIOCL_SETKMSGREDIRECT	11	/* restrict kernel messages to a vt */
#define TIOCL_GETFGCONSOLE	12	/* get foreground vt */
#define TIOCL_SCROLLCONSOLE	13	/* scroll console */
#define TIOCL_BLANKSCREEN	14	/* keep screen blank even if a key is pressed */
#define TIOCL_BLANKEDSCREEN	15	/* return which vt was blanked */
#define TIOCL_GETKMSGREDIRECT	17	/* get the vt kernel messages are restricted to */

#endif /* _LINUX_TIOCL_H */
