/*
 * signal.c — Signal handling for the editline library.
 *
 * Installs handlers for SIGWINCH, SIGINT, SIGQUIT, SIGTSTP, SIGCONT,
 * SIGTERM, and SIGHUP during el_gets().  Old dispositions are saved
 * and restored when el_gets() returns.  Uses sigaction() with
 * SA_RESTART where appropriate.
 */
#include <signal.h>
#include <unistd.h>
#include "el.h"

/* ------------------------------------------------------------------ */
/* Global state (only one EditLine can be active at a time)           */
/* ------------------------------------------------------------------ */

static volatile sig_atomic_t sig_winch;
static volatile sig_atomic_t sig_int;
static EditLine *sig_el;

/* ------------------------------------------------------------------ */
/* Signal handlers                                                    */
/* ------------------------------------------------------------------ */

static void handle_winch(int signo) {
    (void)signo;
    sig_winch = 1;
    if (sig_el)
        sig_el->term.dims_valid = 0;
}

static void handle_int(int signo) {
    (void)signo;
    sig_int = 1;
}

/*
 * SIGTSTP: restore the terminal to its original state, then re-raise
 * with the default disposition so the process actually stops.
 */
static void handle_tstp(int signo) {
    struct sigaction sa;

    if (sig_el) {
        terminal_flush(sig_el);
        terminal_set_orig(sig_el);
    }

    /* Reset to default and re-raise */
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(signo, &sa, NULL);
    kill(getpid(), signo);
    /* -- process stops here, resumes on SIGCONT -- */
}

/*
 * SIGCONT: re-enter raw mode and request a full redraw.
 * Also re-install our SIGTSTP handler.
 */
static void handle_cont(int signo) {
    struct sigaction sa;
    (void)signo;

    if (sig_el) {
        terminal_set_raw(sig_el);
        sig_winch = 1;  /* piggyback: triggers dimension re-query + redraw */
    }

    /* Re-install SIGTSTP handler (was reset to SIG_DFL in handle_tstp) */
    sa.sa_handler = handle_tstp;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  /* no SA_RESTART: allow read() to return EINTR */
    sigaction(SIGTSTP, &sa, NULL);
}

/*
 * SIGTERM / SIGHUP: restore terminal and propagate (clean exit).
 */
static void handle_fatal(int signo) {
    struct sigaction sa;

    if (sig_el) {
        terminal_flush(sig_el);
        terminal_set_orig(sig_el);
    }

    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(signo, &sa, NULL);
    kill(getpid(), signo);
}

/* ------------------------------------------------------------------ */
/* Install / restore API                                              */
/* ------------------------------------------------------------------ */

void el_signals_install(EditLine *el) {
    struct sigaction sa;
    struct signal_state *ss;

    if (!el) return;
    ss = &el->signal_state;
    sig_el = el;
    sig_winch = 0;
    sig_int = 0;

    /* SIGWINCH — no SA_RESTART so read() returns EINTR */
    sa.sa_handler = handle_winch;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGWINCH, &sa, &ss->old_sigwinch);

    /* SIGINT — no SA_RESTART */
    if (ss->active) {
        sa.sa_handler = handle_int;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGINT, &sa, &ss->old_sigint);

        /* SIGQUIT — ignore during editing */
        sa.sa_handler = SIG_IGN;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        sigaction(SIGQUIT, &sa, &ss->old_sigquit);
    }

    /* SIGTSTP — no SA_RESTART */
    sa.sa_handler = handle_tstp;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTSTP, &sa, &ss->old_sigtstp);

    /* SIGCONT — SA_RESTART (fires during stop sequence) */
    sa.sa_handler = handle_cont;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCONT, &sa, &ss->old_sigcont);

    /* SIGTERM — SA_RESTART (handler terminates anyway) */
    sa.sa_handler = handle_fatal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGTERM, &sa, &ss->old_sigterm);

    /* SIGHUP — SA_RESTART (handler terminates anyway) */
    sa.sa_handler = handle_fatal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGHUP, &sa, &ss->old_sighup);
}

void el_signals_restore(EditLine *el) {
    struct signal_state *ss;

    if (!el) return;
    ss = &el->signal_state;

    sigaction(SIGWINCH, &ss->old_sigwinch, NULL);
    if (ss->active) {
        sigaction(SIGINT, &ss->old_sigint, NULL);
        sigaction(SIGQUIT, &ss->old_sigquit, NULL);
    }
    sigaction(SIGTSTP, &ss->old_sigtstp, NULL);
    sigaction(SIGCONT, &ss->old_sigcont, NULL);
    sigaction(SIGTERM, &ss->old_sigterm, NULL);
    sigaction(SIGHUP, &ss->old_sighup, NULL);

    sig_el = NULL;
}

/* ------------------------------------------------------------------ */
/* Flag queries (called from the read loop in readline.c)             */
/* ------------------------------------------------------------------ */

int el_signal_pending(void) {
    return sig_winch || sig_int;
}

void el_signal_handle(EditLine *el) {
    if (!el) return;

    if (sig_int) {
        sig_int = 0;
        /* Discard current line, print ^C, reset buffer, reprint prompt */
        terminal_puts(el, "^C\r\n");
        if (el->prompt)
            terminal_puts(el, el->prompt);
        terminal_flush(el);
        el->line.len = 0;
        el->line.cursor = 0;
        el->line.buffer[0] = '\0';
        el->refresh_rows = 1;
        el->render_cache_len = 0;
    }

    if (sig_winch) {
        sig_winch = 0;
        terminal_get_size(el);
        /* Force full redraw by invalidating cache */
        el->render_cache_len = 0;
    }
}
