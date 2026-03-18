#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include "el.h"

int terminal_set_raw(EditLine *el) {
    if (el->term.is_raw) return 0;

    if (tcgetattr(fileno(el->fin), &el->term.orig) == -1)
        return -1;

    el->term.raw = el->term.orig;
    /* Basic raw mode: disable echo, canonical mode, signals, and extended processing */
    el->term.raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    el->term.raw.c_oflag &= ~(OPOST);
    el->term.raw.c_cflag |= (CS8);
    el->term.raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    el->term.raw.c_cc[VMIN] = 1;
    el->term.raw.c_cc[VTIME] = 0;

    if (tcsetattr(fileno(el->fin), TCSAFLUSH, &el->term.raw) == -1)
        return -1;

    el->term.is_raw = 1;
    return 0;
}

int terminal_set_orig(EditLine *el) {
    if (!el->term.is_raw) return 0;

    if (tcsetattr(fileno(el->fin), TCSAFLUSH, &el->term.orig) == -1)
        return -1;

    el->term.is_raw = 0;
    return 0;
}

void terminal_get_size(EditLine *el) {
    struct winsize ws;
    const char *env;

    if (!el) return;

    /* Try ioctl first */
    if (el->fout &&
        ioctl(fileno(el->fout), TIOCGWINSZ, &ws) == 0 &&
        ws.ws_col > 0 && ws.ws_row > 0) {
        el->term.cols = ws.ws_col;
        el->term.rows = ws.ws_row;
        return;
    }

    /* Fall back to environment variables */
    env = getenv("COLUMNS");
    if (env) {
        int v = atoi(env);
        if (v > 0) el->term.cols = v;
    }
    env = getenv("LINES");
    if (env) {
        int v = atoi(env);
        if (v > 0) el->term.rows = v;
    }

    /* Default 80x24 if nothing else worked */
    if (el->term.cols <= 0) el->term.cols = 80;
    if (el->term.rows <= 0) el->term.rows = 24;
}
