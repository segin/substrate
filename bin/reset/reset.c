/*
 * reset — restore the terminal to a sane state.
 *
 * Matches what the Linux ncurses-bin reset(1) does for the common
 * "my terminal is wedged" case:
 *
 *   1. Reset terminal modes via tcsetattr(TCSAFLUSH) — cooked input
 *      (ICANON), echo on (ECHO), signals + extensions on (ISIG |
 *      IEXTEN), CR-to-NL on input (ICRNL), NL-to-CRNL on output
 *      (OPOST | ONLCR), 8N1 (CS8 | CREAD), VINTR/VQUIT/VERASE/VKILL
 *      bound to ^C / ^\ / ^? / ^U, VMIN=1 VTIME=0.  This is the
 *      `stty sane` line.
 *
 *   2. Emit the VT100/ANSI initialisation sequences that pull a
 *      terminal back from things like having entered the alternate
 *      character set, partially scrolled regions, or hidden cursor:
 *        ESC c           hard reset (RIS — "reset to initial state")
 *        ESC ( B         select US-ASCII into G0
 *        ESC [ ! p       soft terminal reset (DECSTR)
 *        ESC [ ? 25 h    show cursor
 *        ESC [ 2 J       clear screen
 *        ESC [ H         cursor home
 *
 * Argument handling: accepts an optional `TERM` name to humour the
 * historical signature `reset [term]`, but ignores it (we always
 * emit the universal sequence — no terminfo lookup yet).
 *
 * Exits 0 on success.
 */

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static int
write_all(int fd, const char *buf, size_t len)
{
	while (len > 0) {
		ssize_t n = write(fd, buf, len);
		if (n < 0) return -1;
		buf += (size_t)n;
		len -= (size_t)n;
	}
	return 0;
}

static void
set_sane_termios(int fd)
{
	struct termios tio;

	if (tcgetattr(fd, &tio) != 0) {
		return;  /* not a tty, skip silently */
	}

	/* Input flags: BRKINT (signal on break), ICRNL (CR -> NL), IXON
	 * (start/stop output flow control).  Clear IGNBRK, IGNPAR,
	 * PARMRK, ISTRIP, INLCR, IGNCR, IUCLC, IXANY, IXOFF, IMAXBEL. */
	tio.c_iflag &= ~(unsigned)(IGNBRK | PARMRK | ISTRIP | INLCR | IGNCR |
	                            IXOFF);
	tio.c_iflag |= (unsigned)(BRKINT | ICRNL | IXON);

	/* Output: OPOST + ONLCR (NL -> CR/NL).  Clear OLCUC, OCRNL,
	 * ONOCR, ONLRET, OFILL, OFDEL. */
	tio.c_oflag &= ~(unsigned)(OCRNL | ONOCR | ONLRET | OFILL | OFDEL);
	tio.c_oflag |= (unsigned)(OPOST | ONLCR);

	/* Control: 8N1, enable receiver, ignore modem-control lines. */
	tio.c_cflag &= ~(unsigned)(CSIZE | PARENB | PARODD | CSTOPB |
	                            HUPCL | CLOCAL);
	tio.c_cflag |= (unsigned)(CS8 | CREAD | CLOCAL);

	/* Local: cooked + echo + signals + extensions; NO ECHOPRT
	 * (printing terminal echo) or NOFLSH (flush on signal). */
	tio.c_lflag &= ~(unsigned)(ECHONL | NOFLSH | TOSTOP);
	tio.c_lflag |= (unsigned)(ICANON | ISIG | IEXTEN | ECHO | ECHOE |
	                          ECHOK);

	/* Control chars: standard Bourne / Linux mapping. */
	tio.c_cc[VINTR]    = 003;   /* ^C */
	tio.c_cc[VQUIT]    = 034;   /* ^\ */
	tio.c_cc[VERASE]   = 0177;  /* ^? (DEL) */
	tio.c_cc[VKILL]    = 025;   /* ^U */
	tio.c_cc[VEOF]     = 004;   /* ^D */
	tio.c_cc[VSTART]   = 021;   /* ^Q */
	tio.c_cc[VSTOP]    = 023;   /* ^S */
	tio.c_cc[VSUSP]    = 032;   /* ^Z */
	tio.c_cc[VMIN]     = 1;
	tio.c_cc[VTIME]    = 0;

	(void)tcsetattr(fd, TCSAFLUSH, &tio);
}

int
main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;  /* historical [term] argument — ignored. */

	int fd = 2;  /* stderr is typically the controlling tty */
	if (!isatty(fd)) {
		fd = 1;
		if (!isatty(fd)) {
			fd = open("/dev/tty", O_WRONLY);
			if (fd < 0) fd = 1;
		}
	}

	set_sane_termios(fd);

	/* ESC c — RIS (Reset to Initial State).
	 * ESC ( B — designate US-ASCII as G0.
	 * ESC [ ! p — DECSTR (soft terminal reset).
	 * ESC [ ? 25 h — DECTCEM (show cursor).
	 * ESC [ 2 J — ED (erase entire display).
	 * ESC [ H — CUP (cursor to row 1, col 1). */
	const char *seq =
	    "\033c"
	    "\033(B"
	    "\033[!p"
	    "\033[?25h"
	    "\033[2J"
	    "\033[H";
	(void)write_all(fd, seq, strlen(seq));

	return 0;
}
