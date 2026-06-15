/*
 * xargs.h - shared state for substrate xargs.
 *
 * POSIX.1-2024 xargs + GNU + BSD extensions; BSD wins on conflict.
 * See docs/specs/xargs-spec.md.
 */
#ifndef XARGS_H
#define XARGS_H

#include <stddef.h>

/* Item delimiting mode (R-1..R-4). */
enum xa_delim {
    XA_DELIM_WS,    /* default: blank/tab/newline + quoting/backslash (CR-6) */
    XA_DELIM_NUL,   /* -0 : NUL only, no quoting                              */
    XA_DELIM_CHAR   /* -d : single byte, no quoting                           */
};

struct xargs_opts {
    enum xa_delim dmode;
    int           delim_char;   /* XA_DELIM_CHAR byte                          */

    const char   *eofstr;       /* -E/-e logical EOF; NULL = none (CR-1)       */

    const char   *replstr;      /* -I replstr (per-token replace)              */
    const char   *jreplstr;     /* -J replstr (single-point insert)            */
    long          repl_count;   /* -R : max replacements for -I (<0 unlimited) */
    long          repl_size;    /* -S : replacement buffer bytes (0 = auto)    */

    long          max_args;     /* -n : items/invocation (0 = unset)           */
    long          max_lines;    /* -L : lines/invocation (0 = unset)           */
    long          max_chars;    /* -s : command-line byte cap                  */
    long          max_procs;    /* -P : parallel children (1 = serial)         */

    int           f_trace;      /* -t */
    int           f_prompt;     /* -p */
    int           f_exit;       /* -x */
    int           f_norun;      /* -r */
    int           f_opentty;    /* -o */

    const char  **argfiles;     /* -a files (read instead of stdin)            */
    int           nargfiles;
};

/* The operand template: utility + initial arguments (R-10,R-11). */
extern char **g_template;
extern int    g_ntemplate;

/* Worst exit status accumulated across invocations (CR-4 / R-27..R-31). */
extern int    g_exit_status;

/* Set when a fatal child status (124/125) demands we stop reading input. */
extern int    g_stop;

/* The program name for diagnostics. */
extern const char *g_prog;

void xa_fatal(const char *fmt, ...);   /* perror-style message, exit 1 (R-32) */

/* ---- input (xargs_input.c) ---- */
/*
 * Open the input source (stdin or the -a files).  Returns 0 / exits on error.
 */
int  xa_input_open(struct xargs_opts *o);

/*
 * Read the next item.  On success returns 1 and stores a malloc'd string in
 * *out; sets *is_eol=1 when the item ended a logical line (newline) for -L/-I.
 * Returns 0 at end of input (incl. logical EOF, R-7).  Caller frees *out.
 */
int  xa_next_item(struct xargs_opts *o, char **out, int *is_eol);

/* ---- exec (xargs_exec.c) ---- */
/*
 * Run argv (NULL-terminated) honoring -t/-p/-o/-P.  Updates g_exit_status and
 * g_stop per CR-4.  Returns 0 normally, -1 if the prompt declined the command.
 */
int  xa_run(struct xargs_opts *o, char **argv, int argc);

/* Reap all outstanding parallel children (-P). */
void xa_wait_all(struct xargs_opts *o);

#endif /* XARGS_H */
