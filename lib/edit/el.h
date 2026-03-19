#ifndef _EL_H_
#define _EL_H_

#include <stdio.h>
#include <termios.h>
#include <signal.h>
#include <histedit.h>

#define EL_KILL_RING_SIZE 8
#define EL_UNDO_DEPTH 256

enum editor_mode {
    ED_EMACS = 0,
    ED_VI = 1
};

enum vi_mode {
    VI_INSERT = 0,
    VI_COMMAND = 1,
    VI_REPLACE = 2
};

/* Vi repeat info for dot command */
struct vi_repeat {
    char cmd;           /* The command character */
    char arg;           /* Argument char (for r, f, t, etc.) */
    int count;          /* Count prefix */
    char *insert_text;  /* Text inserted (for i/a/c/s type cmds) */
    size_t insert_len;
};

/* Vi find-char state for ;/, repeat */
struct vi_find {
    char ch;            /* Character to find */
    int forward;        /* 1 = f/t, 0 = F/T */
    int till;           /* 1 = t/T, 0 = f/F */
};

/* Vi history search state */
struct vi_search {
    char pattern[256];
    int reverse;        /* 1 = /, 0 = ? */
};

struct terminal {
    struct termios orig;
    struct termios raw;
    int is_raw;
    int cols;
    int rows;
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

struct undo_entry {
    char *buffer;
    size_t len;
    size_t cursor;
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
    int overwrite_mode;
    char *kill_ring[EL_KILL_RING_SIZE];
    size_t kill_ring_count;
    size_t kill_ring_head;
    int last_cmd_was_kill;
    int yank_active;
    size_t yank_start;
    size_t yank_len;
    size_t yank_ring_index;
    struct undo_entry undo_stack[EL_UNDO_DEPTH];
    size_t undo_depth;
    size_t refresh_rows;
    char *render_cache;
    size_t render_cache_cap;
    size_t render_cache_len;
    size_t render_cache_cursor;
    char *saved_input;
    size_t saved_input_cap;
    int history_browsing;
    /* Vi mode state */
    enum vi_mode vi_mode;
    struct vi_repeat vi_repeat;
    struct vi_find vi_find;
    struct vi_search vi_search;
    size_t vi_insert_start;  /* cursor pos when entering insert mode */
};

/*
 * Internal functions.
 *
 * NOTE: EditLine is NOT thread-safe.  A single EditLine handle must only
 * be accessed from one thread at a time (single-thread contract).
 */
int terminal_set_raw(EditLine *el);
int terminal_set_orig(EditLine *el);
void terminal_get_size(EditLine *el);
int line_ensure_capacity(EditLine *el, size_t needed);

#endif /* _EL_H_ */
