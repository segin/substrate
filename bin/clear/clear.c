/*
 * clear — clear the terminal screen and scrollback.
 *
 * Behavior matches the Linux ncurses-bin clear(1) when used against
 * an xterm-class terminal: emit the standard ANSI/VT100 sequences to
 * (1) home the cursor, (2) erase the visible screen, and (3) erase
 * the scrollback buffer.  No terminfo dependency — substrate's
 * libcurses is a stub today, and `clear` against unknown terminals
 * is most useful when it falls back to the universal escape codes
 * anyway.
 *
 * Sequence emitted:
 *   ESC [ H        cursor home  (CUP, default args = 1;1)
 *   ESC [ 2 J      erase entire display
 *   ESC [ 3 J      erase scrollback (xterm extension; ignored by
 *                   terminals that don't understand it)
 *
 * Options:
 *   -x            don't try to clear the scrollback (skip CSI 3 J).
 *   -V, --version print version banner.
 *
 * Exit: 0 always, unless write(1) fails.
 */

#include <stdio.h>
#include <string.h>
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

int
main(int argc, char *argv[])
{
	int clear_scrollback = 1;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-x") == 0) {
			clear_scrollback = 0;
		} else if (strcmp(argv[i], "-V") == 0 ||
		           strcmp(argv[i], "--version") == 0) {
			fputs("clear (substrate base)\n", stdout);
			return 0;
		} else {
			fprintf(stderr, "usage: clear [-x]\n");
			return 1;
		}
	}

	const char *seq = "\033[H\033[2J";
	if (write_all(1, seq, 7) != 0) return 1;
	if (clear_scrollback) {
		if (write_all(1, "\033[3J", 4) != 0) return 1;
	}
	return 0;
}
