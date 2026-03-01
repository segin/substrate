#ifndef _EL_H_
#define _EL_H_

#include <stdio.h>
#include <termios.h>
#include <signal.h>
#include <histedit.h>

enum editor_mode {
    ED_EMACS = 0,
    ED_VI = 1
};

struct terminal {
    struct termios orig;
    struct termios raw;
    int is_raw;
};

struct signal_state {
    int active;
    struct sigaction old_sigint;
    struct sigaction old_sigquit;
    struct sigaction old_sigtstp;
    struct sigaction old_sigcont;
    struct sigaction old_sigwinch;
    struct sigaction old_sigterm;
    struct sigaction old_sighup;
    sigset_t old_mask;
};

struct line {
    char *buffer;
    size_t cap;
    size_t len;
    size_t cursor;
    LineInfo info;
};

struct editline {
    char *prog;
    FILE *fin;
    FILE *fout;
    FILE *ferr;
    struct terminal term;
    struct line line;
    History *history;
    const char *prompt;
    const char *rprompt;
    enum editor_mode editor_mode;
    struct signal_state signal_state;
    unsigned char (*completion)(EditLine *, int);
    void *completion_data;
    void *client_data;
};

/* Internal functions */
int terminal_set_raw(EditLine *el);
int terminal_set_orig(EditLine *el);
int line_ensure_capacity(EditLine *el, size_t needed);

#endif /* _EL_H_ */
