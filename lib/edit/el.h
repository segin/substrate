#ifndef _EL_H_
#define _EL_H_

#include <stdio.h>
#include <termios.h>
#include <histedit.h>

struct terminal {
    struct termios orig;
    struct termios raw;
    int is_raw;
};

struct line {
    char *buffer;
    size_t cap;
    size_t len;
    size_t cursor;
    LineInfo info;
};

struct editline {
    FILE *fin;
    FILE *fout;
    FILE *ferr;
    struct terminal term;
    struct line line;
    History *history;
    const char *prompt;
};

/* Internal functions */
int terminal_set_raw(EditLine *el);
int terminal_set_orig(EditLine *el);

#endif /* _EL_H_ */
